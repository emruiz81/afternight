// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "WorkflowExecutionController.h"

#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/workflow/ManifestBuilder.h"
#include "processes/ImageIntegration/workflow/ExecutionPreparationSnapshot.h"
#include "processes/ImageIntegration/workflow/WorkflowProgressController.h"
#include "processes/ImageIntegration/workflow/WorkflowPreparation.h"
#include "processes/ImageIntegration/workflow/WorkflowRunObserver.h"
#include "processes/ImageIntegration/workflow/WorkflowRunStanding.h"
#include "processes/ImageIntegration/widgets/WorkflowRunView.h"

#include "controls/IAppContext.h"
#include "controls/TopLevelProcessBaseControl.h"
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
#include <cmath>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
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

/// Makes the run state of the process window a workflow is launched from follow the workflow: running from now, and
/// ended - cancelled, failed with its message, or succeeded - by what the returned call is given, which the workflow's
/// end and a preparation that never reached it both call. A launch from no process window follows nothing.
///
/// @param parent the widget the workflow is launched from.
/// @return what ends the run state.
[[nodiscard]] std::function<void(const WorkflowExecutor::Result&)> followRunState(QWidget* parent)
{
    // A process window declares no meta-object of its own, so it is known by its type.
    auto* process = dynamic_cast<TopLevelProcessBaseControl*>(parent);
    if (process == nullptr)
    {
        return [](const WorkflowExecutor::Result&) {};
    }
    process->setRunState(an::ui::runningState(QStringLiteral("running")));
    return [guard = QPointer<TopLevelProcessBaseControl>(process)](const WorkflowExecutor::Result& result) {
        if (!guard)
        {
            return;
        }
        if (result.cancelled)
        {
            guard->setRunState(an::ui::ProcessRunState{.phase = an::ui::ProcessRunState::Phase::Cancelled});
        }
        else if (!result.success)
        {
            guard->setRunState(
                an::ui::ProcessRunState{.phase = an::ui::ProcessRunState::Phase::Failed, .text = result.errorMessage});
        }
        else
        {
            guard->setRunState(an::ui::ProcessRunState{.phase = an::ui::ProcessRunState::Phase::Succeeded});
        }
    };
}

/// Tells the widget a workflow is launched from each progress the run reports, when the widget asks to be told
/// (`IWorkflowRunObserver`), for as long as the widget lives.
void followRunProgress(WorkflowProgressController& controller, QWidget* parent)
{
    auto* const observer = dynamic_cast<IWorkflowRunObserver*>(parent);
    if (observer == nullptr)
    {
        return;
    }
    QObject::connect(&controller, &WorkflowProgressController::progressUpdated, parent,
                     [observer](const WorkflowProgress& progress) { observer->workflowRunProgressed(progress); });
}

/// Makes the run state of the process window a workflow is launched from say where the run stands as its view says
/// it: the step it is at among its stages - "Step 5/9: Registration" - and how far the whole has come, to the percent.
/// A run at none of its stages yet - it prepares - says nothing more than that it runs.
void followRunStanding(WorkflowRunView& view, QWidget* parent)
{
    auto* process = dynamic_cast<TopLevelProcessBaseControl*>(parent);
    if (process == nullptr)
    {
        return;
    }
    QObject::connect(
        &view, &WorkflowRunView::standingChanged, process,
        [guard = QPointer<TopLevelProcessBaseControl>(process), shown = QPointer<WorkflowRunView>(&view)]() {
            if (!guard || !shown || shown->isFinished())
            {
                return;
            }
            const ImageIntegration::WorkflowRunStanding& standing = shown->standing();
            const QString text = ImageIntegration::workflowRunStandingText(standing);
            if (text.isEmpty())
            {
                return;
            }
            guard->reportRunProgress(std::round(standing.fraction * 100.0) / 100.0, text);
        });
}

/// What shows a run: its controller, and its view, which the front end the run is launched from holds.
struct RunFront
{
    WorkflowProgressController* controller = nullptr;
    WorkflowRunView* view = nullptr;
};

/// Makes a run's controller and its view and asks the front end the run is launched from to show the view in itself;
/// the controller then lives with the view. Every front end that launches a run is a host that shows it: one that is
/// not, or that refuses the view, is a fault of the code that launched the run, which no person can act on. It is
/// logged, nothing made is kept, and the run is not started - no run goes on unseen.
///
/// @return what shows the run; none when the front end shows no run.
[[nodiscard]] std::optional<RunFront> openRunFront(IntegrationDataModel* model, QWidget* parent,
                                                   const WorkflowExecutionController::Pipeline& pipeline)
{
    auto* const host = dynamic_cast<IWorkflowRunHost*>(parent);
    if (host == nullptr)
    {
        LOG_ERROR("WorkflowExecutionController: a run launched from a front end that shows no run was not started");
        return std::nullopt;
    }
    auto* const controller = new WorkflowProgressController(model, applicationSession(), nullptr, pipeline);
    auto* const view = new WorkflowRunView(controller);
    if (!host->showWorkflowRun(view))
    {
        delete view;
        delete controller;
        LOG_ERROR("WorkflowExecutionController: the front end refused to show a run, and the run was not started");
        return std::nullopt;
    }
    controller->setParent(view);
    return RunFront{.controller = controller, .view = view};
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

void WorkflowExecutionController::setPipeline(Pipeline pipeline)
{
    m_pipeline = std::move(pipeline);
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
    // A pipeline of the caller's own runs the manifest as it is; the application's needs what a preparation read.
    if (!preparationSnapshot && !m_pipeline)
    {
        return runAsyncPrepared([manifest]() { return manifest; }, std::move(onFinished));
    }

    const std::optional<RunFront> front = openRunFront(m_model, m_parent, m_pipeline);
    if (!front.has_value())
    {
        return false;
    }
    WorkflowProgressController* const controller = front->controller;
    WorkflowRunView* const view = front->view;
    followRunProgress(*controller, m_parent);
    view->setStages(manifest.stageSequence, ImageIntegration::workflowStageWeights(manifest));

    QObject::connect(controller, &WorkflowProgressController::finished, view,
                     [onFinished = std::move(onFinished),
                      endRunState = followRunState(m_parent)](const WorkflowExecutor::Result& result) {
                         endRunState(result);
                         if (onFinished)
                         {
                             onFinished(result);
                         }
                     });
    followRunStanding(*view, m_parent);

    const auto executionManifest = std::make_shared<WorkflowManifest>(manifest);
    QTimer::singleShot(0, view,
                       [controllerPtr = QPointer<WorkflowProgressController>(controller), executionManifest,
                        preparationTiming, preparationSnapshot = std::move(preparationSnapshot)]() mutable {
                           if (controllerPtr)
                           {
                               controllerPtr->start(*executionManifest, preparationTiming,
                                                    std::move(preparationSnapshot));
                           }
                       });
    return true;
}

bool WorkflowExecutionController::runAsyncPrepared(ManifestPreparationCallback prepareManifest,
                                                   CompletionCallback onFinished) const
{
    if (!m_model || !prepareManifest)
    {
        return false;
    }

    const std::optional<RunFront> front = openRunFront(m_model, m_parent, m_pipeline);
    if (!front.has_value())
    {
        return false;
    }
    WorkflowProgressController* const controller = front->controller;
    WorkflowRunView* const view = front->view;
    followRunProgress(*controller, m_parent);
    view->setStages({trImageIntegrationProcess("Preparing")});
    view->onLogMessage(WorkflowLogLevel::Info, trImageIntegrationProcess("Preparing workflow..."));

    auto* watcher = new QFutureWatcher<PreparedWorkflowManifest>(view);
    const auto preparationCancelled = std::make_shared<std::atomic_bool>(false);
    // The run state ends with the workflow, or with a preparation that never reached it.
    const auto endRunState =
        std::make_shared<std::function<void(const WorkflowExecutor::Result&)>>(followRunState(m_parent));
    const auto completion = std::make_shared<CompletionCallback>(
        [endRunState, onFinished = std::move(onFinished)](const WorkflowExecutor::Result& result) {
            (*endRunState)(result);
            if (onFinished)
            {
                onFinished(result);
            }
        });

    QObject::connect(view, &WorkflowRunView::cancelRequested, view,
                     [preparationCancelled]() { preparationCancelled->store(true, std::memory_order_relaxed); });
    QObject::connect(controller, &WorkflowProgressController::finished, view,
                     [completion](const WorkflowExecutor::Result& result) {
                         if (completion && *completion)
                         {
                             (*completion)(result);
                         }
                     });
    followRunStanding(*view, m_parent);
    QObject::connect(
        watcher, &QFutureWatcher<PreparedWorkflowManifest>::finished, view,
        [watcher, viewPtr = QPointer<WorkflowRunView>(view),
         controllerPtr = QPointer<WorkflowProgressController>(controller), parent = QPointer<QWidget>(m_parent),
         completion, preparationCancelled]() {
            watcher->deleteLater();

            if (!viewPtr || !controllerPtr)
            {
                return;
            }

            const auto finishWithoutExecutor = [&](const WorkflowExecutor::Result& result) {
                viewPtr->onFinished(result);
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

            viewPtr->setStages(normalizedManifest.stageSequence,
                               ImageIntegration::workflowStageWeights(normalizedManifest));

            const auto executionManifest = std::make_shared<WorkflowManifest>(std::move(normalizedManifest));
            QTimer::singleShot(0, viewPtr,
                               [controllerPtr, executionManifest, preparationTiming = prepared.preparationTiming,
                                preparationSnapshot = std::move(prepared.preparationSnapshot)]() mutable {
                                   if (controllerPtr)
                                   {
                                       controllerPtr->start(*executionManifest, preparationTiming,
                                                            std::move(preparationSnapshot));
                                   }
                               });
        });

    QTimer::singleShot(
        0, view, [watcher, prepareManifest = std::move(prepareManifest), preparationCancelled]() mutable {
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
