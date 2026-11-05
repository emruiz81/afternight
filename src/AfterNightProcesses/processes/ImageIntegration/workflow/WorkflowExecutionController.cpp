// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "WorkflowExecutionController.h"

#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/workflow/ManifestBuilder.h"
#include "processes/ImageIntegration/workflow/ExecutionPreparationSnapshot.h"
#include "processes/ImageIntegration/workflow/WorkflowProgressController.h"
#include "processes/ImageIntegration/workflow/WorkflowPreparation.h"
#include "processes/ImageIntegration/dialogs/WorkflowProgressDialogV2.h"

#include "controls/IAppContext.h"
#include "session/IEngineSessionProvider.h"

#include <AfterNightPlatform/Logging.h>

#include <QCoreApplication>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QPointer>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <unordered_set>

namespace
{
const QStringList kRegistrationOnlyStages = {QStringLiteral("Bias"), QStringLiteral("Dark"), QStringLiteral("Flat"),
                                             QStringLiteral("LightsCalibration"), QStringLiteral("LightsRegistration")};

using PreparedWorkflowManifest = ImageIntegration::PreparedWorkflowExecution;

/// The session the application lends the workflow's runs; null when no application context provides one.
[[nodiscard]] const an::consumer::EngineSession* applicationSession()
{
    an::ui::IAppContext* context = an::ui::appContext();
    an::ui::IEngineSessionProvider* provider = context != nullptr ? context->getEngineSessionProvider() : nullptr;
    return provider != nullptr ? &provider->engineSession() : nullptr;
}

[[nodiscard]] QString trImageIntegrationProcess(const char* sourceText)
{
    return QCoreApplication::translate("ImageIntegrationProcess", sourceText);
}

void openWarningMessage(QWidget* parent, const QString& title, const QString& text)
{
    auto* messageBox = new QMessageBox(parent);
    messageBox->setIcon(QMessageBox::Warning);
    messageBox->setWindowTitle(title);
    messageBox->setText(text);
    messageBox->setStandardButtons(QMessageBox::Ok);
    messageBox->setAttribute(Qt::WA_DeleteOnClose);
    messageBox->setWindowModality(parent ? Qt::WindowModal : Qt::ApplicationModal);
    messageBox->open();
}

[[nodiscard]] QString validationErrorText(const WorkflowValidationResult& validation)
{
    QStringList lines;
    for (const auto& issue : validation.issues)
    {
        if (issue.severity == ValidationIssue::Severity::Error)
        {
            lines.append(QStringLiteral("• %1").arg(issue.message));
        }
    }

    return lines.isEmpty() ? trImageIntegrationProcess("Invalid workflow manifest.") : lines.join(QStringLiteral("\n"));
}

[[nodiscard]] bool validateManifestAndWarn(QWidget* parent, const WorkflowValidationResult& validation)
{
    if (!validation.hasErrors())
    {
        return true;
    }

    openWarningMessage(parent, trImageIntegrationProcess("Execution Error"), validationErrorText(validation));
    return false;
}

[[nodiscard]] bool validateManifestAndWarn(QWidget* parent, const WorkflowManifest& manifest)
{
    return validateManifestAndWarn(parent, manifest.validate());
}

[[nodiscard]] qint64 elapsedMilliseconds(const std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
}

[[nodiscard]] WorkflowExecutor::Result failedResult(const QString& message,
                                                    const WorkflowPreparationTiming& preparationTiming = {})
{
    WorkflowExecutor::Result result;
    result.preparationTiming = preparationTiming;
    result.success = false;
    result.errorMessage = message;
    if (!message.isEmpty())
    {
        result.errors << message;
    }
    return result;
}

[[nodiscard]] WorkflowExecutor::Result cancelledResult(const WorkflowPreparationTiming& preparationTiming = {})
{
    WorkflowExecutor::Result result;
    result.preparationTiming = preparationTiming;
    result.success = false;
    result.cancelled = true;
    result.errorMessage = trImageIntegrationProcess("Integration cancelled.");
    return result;
}

[[nodiscard]] std::unordered_set<QString> includedLightFramePaths(const IntegrationDataModel& model)
{
    std::unordered_set<QString> included;
    const auto lightFrames = model.lightFrames().frames();
    included.reserve(lightFrames.size());
    for (const auto& frame : lightFrames)
    {
        if (!frame.excluded)
        {
            included.insert(frame.id.canonicalPath());
        }
    }
    return included;
}

void filterRegistrationGroupToIncludedLights(WorkflowManifest::RegistrationGroup& group,
                                             const std::unordered_set<QString>& includedLightPaths)
{
    std::vector<FrameId> lightFiles;
    std::vector<double> lightExposureSeconds;
    std::vector<QString> lightCaptureTimestampsUtc;
    lightFiles.reserve(group.lightFiles.size());
    lightExposureSeconds.reserve(group.lightFiles.size());
    lightCaptureTimestampsUtc.reserve(group.lightFiles.size());

    for (size_t i = 0; i < group.lightFiles.size(); ++i)
    {
        const FrameId& light = group.lightFiles[i];
        if (includedLightPaths.find(light.canonicalPath()) == includedLightPaths.end())
        {
            continue;
        }

        lightFiles.push_back(light);
        lightExposureSeconds.push_back(i < group.lightExposureSeconds.size() ? group.lightExposureSeconds[i] : 0.0);
        lightCaptureTimestampsUtc.push_back(
            i < group.lightCaptureTimestampsUtc.size() ? group.lightCaptureTimestampsUtc[i] : QString());
    }

    group.lightFiles = std::move(lightFiles);
    group.lightExposureSeconds = std::move(lightExposureSeconds);
    group.lightCaptureTimestampsUtc = std::move(lightCaptureTimestampsUtc);

    if (group.referenceFrame.has_value())
    {
        const QString referencePath = group.referenceFrame->canonicalPath();
        const bool referenceStillIncluded =
            std::any_of(group.lightFiles.cbegin(), group.lightFiles.cend(),
                        [&](const FrameId& id) { return id.canonicalPath() == referencePath; });
        if (!referenceStillIncluded)
        {
            group.referenceFrame.reset();
            group.settings.referenceImage.clear();
        }
    }
}

void sanitizeRegistrationManifestForCurrentLightSelection(const IntegrationDataModel& model, WorkflowManifest& manifest)
{
    const std::unordered_set<QString> includedLights = includedLightFramePaths(model);

    for (auto& group : manifest.registrationGroups)
    {
        filterRegistrationGroupToIncludedLights(group, includedLights);
    }

    manifest.registrationGroups.erase(
        std::remove_if(manifest.registrationGroups.begin(), manifest.registrationGroups.end(),
                       [](const WorkflowManifest::RegistrationGroup& group) { return group.lightFiles.empty(); }),
        manifest.registrationGroups.end());

    manifest.lightsCalibration.erase(
        std::remove_if(manifest.lightsCalibration.begin(), manifest.lightsCalibration.end(),
                       [&](const WorkflowManifest::LightsCalibrationParams& params) {
                           return includedLights.find(params.lightPath.canonicalPath()) == includedLights.end();
                       }),
        manifest.lightsCalibration.end());
}
} // namespace

WorkflowExecutionController::WorkflowExecutionController(IntegrationDataModel* model, QWidget* parent)
    : m_model(model), m_parent(parent)
{
}

void WorkflowExecutionController::normalizeManifestForExecution(
    WorkflowManifest& manifest, const std::optional<std::uint64_t> availablePhysicalBytes)
{
    ImageIntegration::normalizeManifestForExecution(manifest, availablePhysicalBytes);
}

bool WorkflowExecutionController::validateManifestAndWarn(const WorkflowManifest& manifest) const
{
    return ::validateManifestAndWarn(m_parent, manifest);
}

bool WorkflowExecutionController::runAsyncValidated(
    const WorkflowManifest& manifest, CompletionCallback onFinished, WorkflowPreparationTiming preparationTiming,
    std::shared_ptr<const ExecutionPreparationSnapshot> preparationSnapshot) const
{
    if (!m_model)
    {
        return false;
    }

    WorkflowManifest normalizedManifest = manifest;
    WorkflowExecutionController::normalizeManifestForExecution(normalizedManifest);

    if (!validateManifestAndWarn(normalizedManifest))
    {
        return false;
    }

    if (!preparationSnapshot)
    {
        return runAsyncPrepared([normalizedManifest]() { return normalizedManifest; }, std::move(onFinished));
    }
    return runAsyncPrevalidated(normalizedManifest, std::move(onFinished), preparationTiming,
                                std::move(preparationSnapshot));
}

bool WorkflowExecutionController::runAsyncPrevalidated(
    const WorkflowManifest& manifest, CompletionCallback onFinished, WorkflowPreparationTiming preparationTiming,
    std::shared_ptr<const ExecutionPreparationSnapshot> preparationSnapshot) const
{
    if (!m_model)
    {
        return false;
    }
    if (!preparationSnapshot)
    {
        return runAsyncPrepared([manifest]() { return manifest; }, std::move(onFinished));
    }

    auto* controller = new WorkflowProgressController(m_model, applicationSession());
    auto* dialog = new WorkflowProgressDialogV2(controller, m_parent);
    controller->setParent(dialog);
    dialog->setStages(manifest.stageSequence);

    QObject::connect(controller, &WorkflowProgressController::finished, dialog,
                     [onFinished = std::move(onFinished)](const WorkflowExecutor::Result& result) {
                         if (onFinished)
                         {
                             onFinished(result);
                         }
                     });
    QObject::connect(dialog, &QDialog::finished, dialog, [dialog](int) { dialog->deleteLater(); });

    const auto executionManifest = std::make_shared<WorkflowManifest>(manifest);
    QTimer::singleShot(0, dialog,
                       [controllerPtr = QPointer<WorkflowProgressController>(controller), executionManifest,
                        preparationTiming, preparationSnapshot = std::move(preparationSnapshot)]() mutable {
                           if (controllerPtr)
                           {
                               controllerPtr->start(*executionManifest, preparationTiming,
                                                    std::move(preparationSnapshot));
                           }
                       });
    dialog->open();
    return true;
}

bool WorkflowExecutionController::runAsyncPrepared(ManifestPreparationCallback prepareManifest,
                                                   CompletionCallback onFinished) const
{
    if (!m_model || !prepareManifest)
    {
        return false;
    }

    auto* controller = new WorkflowProgressController(m_model, applicationSession());
    auto* dialog = new WorkflowProgressDialogV2(controller, m_parent);
    controller->setParent(dialog);
    dialog->setStages({trImageIntegrationProcess("Preparing")});
    dialog->onLogMessage(WorkflowLogLevel::Info, trImageIntegrationProcess("Preparing workflow..."));

    auto* watcher = new QFutureWatcher<PreparedWorkflowManifest>(dialog);
    const auto preparationCancelled = std::make_shared<std::atomic_bool>(false);
    const auto completion = std::make_shared<CompletionCallback>(std::move(onFinished));

    QObject::connect(dialog, &WorkflowProgressDialogV2::cancelRequested, dialog,
                     [preparationCancelled]() { preparationCancelled->store(true, std::memory_order_relaxed); });
    QObject::connect(controller, &WorkflowProgressController::finished, dialog,
                     [completion](const WorkflowExecutor::Result& result) {
                         if (completion && *completion)
                         {
                             (*completion)(result);
                         }
                     });
    QObject::connect(dialog, &QDialog::finished, dialog, [dialog](int) { dialog->deleteLater(); });
    QObject::connect(
        watcher, &QFutureWatcher<PreparedWorkflowManifest>::finished, dialog,
        [watcher, dialogPtr = QPointer<WorkflowProgressDialogV2>(dialog),
         controllerPtr = QPointer<WorkflowProgressController>(controller), parent = QPointer<QWidget>(m_parent),
         completion, preparationCancelled]() {
            watcher->deleteLater();

            if (!dialogPtr || !controllerPtr)
            {
                return;
            }

            const auto finishWithoutExecutor = [&](const WorkflowExecutor::Result& result) {
                dialogPtr->onFinished(result);
                if (completion && *completion)
                {
                    (*completion)(result);
                }
            };

            PreparedWorkflowManifest prepared = watcher->result();
            if (preparationCancelled->load(std::memory_order_relaxed))
            {
                finishWithoutExecutor(cancelledResult(prepared.preparationTiming));
                return;
            }

            if (!prepared.success)
            {
                const QString message = prepared.errorMessage.trimmed();
                finishWithoutExecutor(failedResult(
                    message.isEmpty() ? trImageIntegrationProcess("Workflow preparation failed.") : message,
                    prepared.preparationTiming));
                return;
            }

            WorkflowManifest normalizedManifest = std::move(prepared.manifest);
            if (!::validateManifestAndWarn(parent.data(), prepared.validation))
            {
                finishWithoutExecutor(
                    failedResult(trImageIntegrationProcess("Invalid workflow manifest."), prepared.preparationTiming));
                return;
            }

            dialogPtr->setStages(normalizedManifest.stageSequence);

            const auto executionManifest = std::make_shared<WorkflowManifest>(std::move(normalizedManifest));
            QTimer::singleShot(0, dialogPtr,
                               [controllerPtr, executionManifest, preparationTiming = prepared.preparationTiming,
                                preparationSnapshot = std::move(prepared.preparationSnapshot)]() mutable {
                                   if (controllerPtr)
                                   {
                                       controllerPtr->start(*executionManifest, preparationTiming,
                                                            std::move(preparationSnapshot));
                                   }
                               });
        });

    dialog->open();
    QTimer::singleShot(
        0, dialog, [watcher, prepareManifest = std::move(prepareManifest), preparationCancelled]() mutable {
            watcher->setFuture(QtConcurrent::run([prepareManifest = std::move(prepareManifest), preparationCancelled,
                                                  session = applicationSession()]() mutable {
                return ImageIntegration::prepareDirectWorkflowExecution(
                    prepareManifest,
                    [preparationCancelled]() { return preparationCancelled->load(std::memory_order_relaxed); },
                    session != nullptr ? ImageIntegration::sessionFrameHeaderReader(*session)
                                       : ImageIntegration::FrameHeaderReader{});
            }));
        });

    return true;
}

void WorkflowExecutionController::filterRegistrationOnlyStageSequence(WorkflowManifest& manifest)
{
    QStringList filteredSequence;
    for (const auto& stage : manifest.stageSequence)
    {
        if (kRegistrationOnlyStages.contains(stage))
        {
            filteredSequence.append(stage);
        }
    }
    manifest.stageSequence = filteredSequence;
}

bool WorkflowExecutionController::buildRegistrationOnlyManifestForGroup(const QString& registrationGroupKey,
                                                                        bool reprocessExisting,
                                                                        WorkflowManifest& outManifest) const
{
    if (!m_model)
    {
        return false;
    }

    ManifestBuilder builder(*m_model);
    builder.setReprocessExisting(reprocessExisting);
    builder.setIncludeBiasFrames(true);
    builder.setIncludeDarkFrames(true);
    builder.setIncludeFlatFrames(true);
    builder.setIncludeLightFrames(true);
    outManifest = builder.build();
    sanitizeRegistrationManifestForCurrentLightSelection(*m_model, outManifest);

    auto it = std::find_if(outManifest.registrationGroups.begin(), outManifest.registrationGroups.end(),
                           [&registrationGroupKey](const WorkflowManifest::RegistrationGroup& group) {
                               return group.groupKey.toString() == registrationGroupKey;
                           });

    if (it == outManifest.registrationGroups.end())
    {
        openWarningMessage(
            m_parent, trImageIntegrationProcess("Execution Error"),
            trImageIntegrationProcess("The selected registration group was not found in the workflow manifest."));
        return false;
    }

    WorkflowManifest::RegistrationGroup selectedGroup = std::move(*it);
    outManifest.registrationGroups.clear();
    outManifest.registrationGroups.push_back(std::move(selectedGroup));
    outManifest.integrationGroups.clear();
    outManifest.executionScope = QStringLiteral("registration_group");
    outManifest.executionScopeKey = registrationGroupKey;

    filterRegistrationOnlyStageSequence(outManifest);

    std::unordered_set<FrameId> selectedLights;
    selectedLights.reserve(outManifest.registrationGroups.front().lightFiles.size());
    for (const auto& light : outManifest.registrationGroups.front().lightFiles)
    {
        selectedLights.insert(light);
    }

    auto& lightsCalibration = outManifest.lightsCalibration;
    lightsCalibration.erase(std::remove_if(lightsCalibration.begin(), lightsCalibration.end(),
                                           [&selectedLights](const WorkflowManifest::LightsCalibrationParams& params) {
                                               return selectedLights.find(params.lightPath) == selectedLights.end();
                                           }),
                            lightsCalibration.end());

    WorkflowExecutionController::normalizeManifestForExecution(outManifest);

    return true;
}

WorkflowManifest WorkflowExecutionController::buildRegistrationOnlyManifestAll(bool reprocessExisting) const
{
    if (!m_model)
    {
        return {};
    }

    ManifestBuilder builder(*m_model);
    builder.setReprocessExisting(reprocessExisting);
    builder.setIncludeBiasFrames(true);
    builder.setIncludeDarkFrames(true);
    builder.setIncludeFlatFrames(true);
    builder.setIncludeLightFrames(true);

    WorkflowManifest manifest = builder.build();
    sanitizeRegistrationManifestForCurrentLightSelection(*m_model, manifest);
    manifest.integrationGroups.clear();
    manifest.executionScope = QStringLiteral("registration_all");
    manifest.executionScopeKey.clear();
    filterRegistrationOnlyStageSequence(manifest);
    WorkflowExecutionController::normalizeManifestForExecution(manifest);
    return manifest;
}
