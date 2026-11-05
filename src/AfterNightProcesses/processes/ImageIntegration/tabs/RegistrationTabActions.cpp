// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "RegistrationTab.h"
#include "RegistrationTabUiBuilder.h"
#include "TabStyling.h"

#include "controls/IAppContext.h"
#include "processes/ImageIntegration/controls/RegistrationSettingsDialog.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/FrameQuality.h"
#include "processes/ImageIntegration/model/FilterStatistics.h"
#include "processes/ImageIntegration/model/RegistrationGroupEngine.h"
#include "processes/ImageIntegration/model/UndoCommands.h"
#include "processes/ImageIntegration/core/BlinkComparatorExclusionUtils.h"
#include "processes/ImageIntegration/core/RegistrationSettingsUtils.h"
#include "processes/ImageIntegration/dialogs/BlinkComparatorDialog.h"
#include "processes/ImageIntegration/dialogs/CometTrackingDialogAutoTrackAdapter.h"
#include "processes/ImageIntegration/workflow/RegistrationWorkAnalyzer.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutionController.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationMetricsSchema.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationFrames.h"
#include "processes/core/RegistrationMetricsWriter.h"
#include "processes/core/RegistrationRun.h"
#include "RegistrationMetricsDataAccess.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightPlatform/Logging.h>

#include <QBoxLayout>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QGroupBox>
#include <QHeaderView>
#include <QIcon>
#include <QHash>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFutureWatcher>
#include <QFrame>
#include <QLabel>
#include <QPalette>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QVector>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QPointer>
#include <QShortcut>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSet>
#include <QStringList>
#include <QStyle>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QUndoStack>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <unordered_map>

#include "processes/ImageIntegration/ui_models/GroupTreeModel.h"
#include "processes/ImageIntegration/widgets/FrameTreeView.h"
#include "processes/ImageIntegration/widgets/QualityDistributionWidget.h"
#include "theme/AnDesignTokens.h"

#include "RegistrationTabImpl.h"

namespace
{

/// Asks a question without a nested event loop: the box is shown, and `answered` receives the button the person chose
/// once they chose it. The process runs inside a host whose event loop must keep answering, so no modal call waits.
void askWithoutBlocking(QWidget* parent, const QMessageBox::Icon icon, const QString& title, const QString& text,
                        const QMessageBox::StandardButtons buttons, const QMessageBox::StandardButton defaultButton,
                        std::function<void(QMessageBox::StandardButton)> answered)
{
    auto* box = new QMessageBox(icon, title, text, buttons, parent);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setDefaultButton(defaultButton);
    QObject::connect(box, &QDialog::finished, box, [box, answered = std::move(answered)](const int result) {
        QMessageBox::StandardButton button = box->standardButton(box->clickedButton());
        if (button == QMessageBox::NoButton)
        {
            button = static_cast<QMessageBox::StandardButton>(result);
        }
        if (answered)
        {
            answered(button);
        }
    });
    box->open();
}

/// Runs one of the settings dialog's comet searches on a worker, in a batch operation of the process's session: every
/// frame the search reads is read as the registration reads it - Float32 fractions of full scale, a mosaic demosaiced
/// by `demosaic` - as an image of that operation's engine.
///
/// @param session the process's session, or null where none is lent.
/// @param demosaic how a mosaic frame is demosaiced.
/// @param request the search's frames and settings; its reader is set here.
/// @param search the search, called with the operation's execution and the request.
/// @return what the search answered, or why it could not run.
template <typename Request, typename Search>
[[nodiscard]] an::ui::CometTrackingDialogAutoTrackResult
runCometDialogSearch(const an::consumer::EngineSession* session,
                     const an::ui::processes::RegistrationDemosaic& demosaic, Request request, Search search)
{
    an::ui::CometTrackingDialogAutoTrackResult result;
    if (session == nullptr)
    {
        result.message = QStringLiteral("The comet frames cannot be read: no engine session is available.");
        return result;
    }

    an::Result<an::consumer::OperationScope> scope =
        session->admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch});
    if (!scope.hasValue())
    {
        result.message =
            QStringLiteral("The comet frames cannot be read: %1").arg(QString::fromUtf8(scope.status().message()));
        return result;
    }

    const an::consumer::OperationScope& operation = *scope;
    request.frameReader = [&operation, &demosaic](const an::ui::CometTrackingFrameContext& context) {
        return stage_utils::readRegistrationImage(operation, nullptr, context.refinementSourcePath, nullptr, demosaic);
    };
    return search(operation.execution(), request);
}

} // namespace

void RegistrationTab::Impl::showFrameContextMenu(const QPoint& pos)
{
    const RegistrationGroupEntry* group = selectedGroup();
    if (!group || !ui.tblFrameQuality)
        return;

    const auto selectedRows = ui.tblFrameQuality->selectionModel()->selectedRows();
    if (selectedRows.isEmpty())
        return;

    QMenu menu(q);

    // Single selection actions
    if (selectedRows.size() == 1)
    {
        const int row = selectedRows.first().row();
        auto* frameItem = ui.tblFrameQuality->item(row, kFrameCol);
        const QString canonical = frameItem ? frameItem->data(Qt::UserRole).toString().trimmed() : QString();
        if (!canonical.isEmpty())
        {
            QAction* showInLightsTreeAction =
                menu.addAction(integrationIcon(QStringLiteral("next")), RegistrationTab::tr("Show in Lights Tree"));
            QObject::connect(showInLightsTreeAction, &QAction::triggered, [this, canonical]() {
                // Navigate to the Lights tab so the selection is visible in the tree.
                QWidget* top = q ? q->window() : nullptr;
                auto* rootTabs = top ? top->findChild<QTabWidget*>(QStringLiteral("rootTabWidget")) : nullptr;
                auto* sourceTabs = top ? top->findChild<QTabWidget*>(QStringLiteral("sourceTabWidget")) : nullptr;
                auto* mainTabs = top ? top->findChild<QTabWidget*>(QStringLiteral("mainTabWidget")) : nullptr;
                if (rootTabs)
                {
                    rootTabs->setCurrentIndex(0); // Frame Sources
                }

                auto* tabs = sourceTabs ? sourceTabs : mainTabs;
                if (tabs)
                {
                    tabs->setCurrentIndex(static_cast<int>(FrameType::Light));
                }

                model->setSelectedFrameIds({FrameId(canonical)});

                // Ensure the tree expands and scrolls to the frame.
                QTimer::singleShot(0, q, [tabs, canonical]() {
                    if (!tabs)
                    {
                        return;
                    }

                    QWidget* lightTabWidget = tabs->widget(static_cast<int>(FrameType::Light));
                    if (!lightTabWidget)
                    {
                        return;
                    }

                    auto* tree = lightTabWidget->findChild<FrameTreeView*>();
                    if (!tree || !tree->selectionModel())
                    {
                        return;
                    }

                    auto* treeModel = qobject_cast<GroupTreeModel*>(tree->model());
                    if (!treeModel)
                    {
                        return;
                    }

                    const QModelIndex idx = treeModel->findIndexForPath(canonical);
                    if (!idx.isValid())
                    {
                        return;
                    }

                    for (QModelIndex parent = idx.parent(); parent.isValid(); parent = parent.parent())
                    {
                        tree->expand(parent);
                    }

                    tree->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect |
                                                                     QItemSelectionModel::Rows);
                    tree->scrollTo(idx, QAbstractItemView::PositionAtCenter);
                    tree->setFocus(Qt::OtherFocusReason);
                });
            });

            menu.addSeparator();
        }

        if (!canonical.isEmpty())
        {
            const bool isInGroup =
                std::any_of(group->frames.begin(), group->frames.end(),
                            [&canonical](const FrameId& id) { return id.canonicalPath() == canonical; });
            if (isInGroup)
            {
                const FrameId frameId(canonical);
                bool isExcluded = false;
                if (auto info = model->lightFrames().findById(frameId); info.has_value())
                {
                    isExcluded = info->excluded;
                }

                QString currentRef;
                if (auto ref = model->registrationReferenceFrame(group->key); ref.has_value())
                {
                    currentRef = ref->canonicalPath();
                }
                const bool isReference = (frameId.canonicalPath() == currentRef);

                QAction* setRefAction = menu.addAction(integrationIcon(QStringLiteral("star_filled")),
                                                       RegistrationTab::tr("Set as Registration Reference"));
                setRefAction->setEnabled(!isReference && !isExcluded);
                QObject::connect(setRefAction, &QAction::triggered, [this]() {
                    if (ui.btnSetReference && ui.btnSetReference->isEnabled())
                        ui.btnSetReference->click();
                });

                menu.addSeparator();
            }
        }
    }

    // Multi-selection actions
    const auto selectedByExclusion = selectedFramePathsByExclusion();

    if (!selectedByExclusion.included.isEmpty())
    {
        const int count = selectedByExclusion.included.size();
        QAction* excludeAction = menu.addAction(integrationIcon(QStringLiteral("delete")),
                                                count == 1 ? RegistrationTab::tr("Exclude Frame")
                                                           : RegistrationTab::tr("Exclude %1 Frames").arg(count));
        const QStringList includedPaths = selectedByExclusion.included;
        QObject::connect(excludeAction, &QAction::triggered,
                         [this, includedPaths]() { setFramesExcluded(includedPaths, true); });
    }

    if (!selectedByExclusion.excluded.isEmpty())
    {
        const int count = selectedByExclusion.excluded.size();
        QAction* includeAction = menu.addAction(integrationIcon(QStringLiteral("add")),
                                                count == 1 ? RegistrationTab::tr("Include Frame")
                                                           : RegistrationTab::tr("Include %1 Frames").arg(count));
        const QStringList excludedPaths = selectedByExclusion.excluded;
        QObject::connect(includeAction, &QAction::triggered,
                         [this, excludedPaths]() { setFramesExcluded(excludedPaths, false); });
    }

    if (!menu.actions().isEmpty() && !menu.actions().back()->isSeparator())
    {
        menu.addSeparator();
    }

    // Preview action
    QAction* previewAction = menu.addAction(integrationIcon(QStringLiteral("picture")),
                                            RegistrationTab::tr("Preview in Blink Comparator..."));
    QObject::connect(previewAction, &QAction::triggered, [this]() {
        if (!ui.tblFrameQuality)
        {
            return;
        }

        const auto selectedRows = ui.tblFrameQuality->selectionModel()->selectedRows();
        if (selectedRows.isEmpty())
        {
            return;
        }

        QStringList paths;
        paths.reserve(selectedRows.size());

        for (const auto& idx : selectedRows)
        {
            const int row = idx.row();
            auto* item = ui.tblFrameQuality->item(row, kFrameCol);
            if (!item)
            {
                continue;
            }
            const QString canonical = item->data(Qt::UserRole).toString().trimmed();
            if (!canonical.isEmpty())
            {
                paths.append(canonical);
            }
        }

        paths.removeDuplicates();
        if (paths.isEmpty())
        {
            return;
        }

        auto* dialog = new BlinkComparatorDialog(paths, q);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        const QPointer<BlinkComparatorDialog> dialogPtr(dialog);
        QObject::connect(dialog, &QDialog::finished, q, [this, dialogPtr]() {
            if (!dialogPtr)
            {
                return;
            }
            // false = no exclusions to apply (not an error); changes are undo-aware.
            (void)ImageIntegration::blink::applyExclusions(model, undoStack, FrameType::Light,
                                                           dialogPtr->comparisonResult().excludedPaths);
        });
        dialog->open();
    });

    menu.exec(ui.tblFrameQuality->viewport()->mapToGlobal(pos));
}

void RegistrationTab::Impl::showGroupContextMenu(const QPoint& pos)
{
    const RegistrationGroupEntry* group = selectedGroup();
    if (!group || !ui.tblRegistrationGroups)
        return;

    QMenu menu(q);

    QAction* settingsAction =
        menu.addAction(integrationIcon(QStringLiteral("settings")), RegistrationTab::tr("Registration Settings..."));
    QObject::connect(settingsAction, &QAction::triggered, [this]() { openSettingsDialogForSelectedGroup(); });

    menu.addSeparator();

    QAction* runAction =
        menu.addAction(integrationIcon(QStringLiteral("play")), RegistrationTab::tr("Run Registration"));
    runAction->setEnabled(btnRunRegistrationGroup && btnRunRegistrationGroup->isEnabled());
    QObject::connect(runAction, &QAction::triggered, [this]() {
        if (btnRunRegistrationGroup)
            btnRunRegistrationGroup->click();
    });

    menu.addSeparator();

    QAction* selectAllAction = menu.addAction(RegistrationTab::tr("Select All Frames in Group"));
    QObject::connect(selectAllAction, &QAction::triggered, [this]() {
        if (ui.tblFrameQuality)
            ui.tblFrameQuality->selectAll();
    });

    menu.exec(ui.tblRegistrationGroups->viewport()->mapToGlobal(pos));
}

void RegistrationTab::Impl::updateThresholdPreview()
{
    const RegistrationGroupEntry* group = selectedGroup();
    QLabel* previewLabel = ui.lblThresholdPreview;

    if (!group || !previewLabel)
    {
        if (previewLabel)
            previewLabel->clear();
        return;
    }

    const QualityThresholdConfig thresholds = currentQualityThresholdConfig();
    const FrameScoreContext scoreContext = buildFrameScoreContext(*group);

    // Count frames that would be excluded
    int wouldExclude = 0;
    int totalWithMetrics = 0;

    for (const auto& frameId : group->frames)
    {
        const auto metrics = model->frameQualityMetrics(frameId);
        if (!metrics.has_value() || !metrics->succeeded)
            continue;

        ++totalWithMetrics;

        const double score = qualityScoreForFrame(frameId, *metrics, scoreContext);
        if (frameFailsQualityThresholds(*metrics, score, thresholds))
            ++wouldExclude;
    }

    if (!thresholds.anyEnabled())
    {
        previewLabel->setText(RegistrationTab::tr("Set filters to evaluate frames"));
        applyStatusTone(previewLabel, "secondary");
    }
    else if (totalWithMetrics == 0)
    {
        previewLabel->setText(RegistrationTab::tr("No quality metrics available"));
        applyStatusTone(previewLabel, "secondary");
    }
    else if (wouldExclude == 0)
    {
        previewLabel->setText(RegistrationTab::tr("✓ All %1 frames pass").arg(totalWithMetrics));
        applyStatusTone(previewLabel, "success");
    }
    else
    {
        previewLabel->setText(
            RegistrationTab::tr("⚠ %1 of %2 frames would be excluded").arg(wouldExclude).arg(totalWithMetrics));
        applyStatusTone(previewLabel, "warning");
    }
}

void RegistrationTab::Impl::openSettingsDialogForSelectedGroup()
{
    const RegistrationGroupEntry* group = selectedGroup();
    if (!group)
    {
        return;
    }

    const GroupKey machine_key = RegistrationGroupEngine::toMachineGroupKey(group->key);
    const QString group_label = group->key.toString();
    const RegistrationGroupKey group_key_struct = group->key;

    // Collect available light frame paths for reference selection
    QStringList light_paths;
    light_paths.reserve(static_cast<int>(group->frames.size()));
    for (const auto& frame : group->frames)
    {
        light_paths.append(frame.canonicalPath());
    }

    // Shown without a nested event loop: the process runs inside a host whose event loop must keep answering, so the
    // settings are saved when the dialog is accepted rather than when a modal call returns.
    auto* dialog = new RegistrationSettingsDialog(group_label, light_paths, q);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setRegistrationGroupContext(group->key.resolution, group->key.objectName);

    // Load existing settings if any
    auto it = model->registrationSettings().find(machine_key);
    if (it != model->registrationSettings().end())
    {
        const RegistrationSettings& settings = it->second;

        dialog->setRegistrationSettings(settings);
        dialog->setAutoReferenceDuringRegistration(settings.autoReferenceDuringRegistration);
    }
    else
    {
        dialog->setAutoReferenceDuringRegistration(true);
    }

    dialog->setCometFramePreviewSizeProvider([this](const QString& frame_id) -> std::optional<QSize> {
        if (model == nullptr || frame_id.trimmed().isEmpty())
        {
            return std::nullopt;
        }

        const FrameId id(frame_id);
        if (!id.isValid())
        {
            return std::nullopt;
        }

        if (const std::optional<FrameInfo> frame = model->lightFrames().findById(id))
        {
            const QSize dimensions = frame->metadata.dimensions();
            if (dimensions.width() > 0 && dimensions.height() > 0)
            {
                return dimensions;
            }
        }

        return std::nullopt;
    });

    // The comet searches read the group's frames as the registration reads them, demosaiced as the dialog states now.
    const auto cometFrameDemosaic = [this, dialog, machine_key]() {
        RegistrationSettings current = settingsForGroup(machine_key);
        current.cfaPattern = dialog->getCFAPattern();
        current.debayerMethod = dialog->getDebayerMethod();
        return an::ui::processes::registrationDemosaicOf(current);
    };

    dialog->setCometAutoTrackHandler([this, dialog, group_key_struct,
                                      cometFrameDemosaic](const RegistrationSettings::CometTrackingSettings& settings,
                                                          an::ui::CometTrackingAutoTrackCompletion done) mutable {
        auto groups_now = collectRegistrationGroups();
        const auto group_it =
            std::find_if(groups_now.begin(), groups_now.end(),
                         [&](const RegistrationGroupEntry& entry) { return entry.key == group_key_struct; });

        an::ui::CometTrackingDialogAutoTrackRequest request;
        request.referenceFrame = FrameId(dialog->getReferenceImage());
        request.settings = settings;
        if (group_it != groups_now.end())
        {
            request.frames.reserve(group_it->frames.size());
            for (const FrameId& frame_id : group_it->frames)
            {
                if (const std::optional<FrameInfo> frame = model->lightFrames().findById(frame_id))
                {
                    request.frames.push_back(*frame);
                }
            }
        }

        const an::ui::processes::RegistrationDemosaic demosaic = cometFrameDemosaic();
        const an::consumer::EngineSession* session = ImageIntegration::detail::applicationEngineSession();
        const QPointer<RegistrationSettingsDialog> dialogGuard(dialog);
        auto* watcher = new QFutureWatcher<an::ui::CometTrackingDialogAutoTrackResult>(q);
        QObject::connect(watcher, &QFutureWatcher<an::ui::CometTrackingDialogAutoTrackResult>::finished, q,
                         [watcher, dialogGuard, done = std::move(done)]() mutable {
                             an::ui::CometTrackingDialogAutoTrackResult result = watcher->result();
                             watcher->deleteLater();
                             // The dialog that asked may have closed while the frames were read.
                             if (dialogGuard)
                             {
                                 done(std::move(result.diagnostics), std::move(result.message));
                             }
                         });
        watcher->setFuture(QtConcurrent::run([request = std::move(request), session, demosaic]() {
            return runCometDialogSearch(
                session, demosaic, request,
                [](const an::ExecutionContext& execution, const an::ui::CometTrackingDialogAutoTrackRequest& admitted) {
                    return an::ui::runCometTrackingDialogAutoTrack(execution, admitted);
                });
        }));
    });

    dialog->setCometBlindDetectHandler([this, dialog, group_key_struct,
                                        cometFrameDemosaic](const RegistrationSettings::CometTrackingSettings& settings,
                                                            an::ui::CometTrackingAutoTrackCompletion done) mutable {
        auto groups_now = collectRegistrationGroups();
        const auto group_it =
            std::find_if(groups_now.begin(), groups_now.end(),
                         [&](const RegistrationGroupEntry& entry) { return entry.key == group_key_struct; });

        an::ui::CometTrackingDialogBlindDetectRequest request;
        request.referenceFrame = FrameId(dialog->getReferenceImage());
        request.settings = settings;
        if (group_it != groups_now.end())
        {
            request.frames.reserve(group_it->frames.size());
            for (const FrameId& frame_id : group_it->frames)
            {
                if (const std::optional<FrameInfo> frame = model->lightFrames().findById(frame_id))
                {
                    request.frames.push_back(*frame);
                }
            }
        }

        const an::ui::processes::RegistrationDemosaic demosaic = cometFrameDemosaic();
        const an::consumer::EngineSession* session = ImageIntegration::detail::applicationEngineSession();
        const QPointer<RegistrationSettingsDialog> dialogGuard(dialog);
        auto* watcher = new QFutureWatcher<an::ui::CometTrackingDialogAutoTrackResult>(q);
        QObject::connect(watcher, &QFutureWatcher<an::ui::CometTrackingDialogAutoTrackResult>::finished, q,
                         [watcher, dialogGuard, done = std::move(done)]() mutable {
                             an::ui::CometTrackingDialogAutoTrackResult result = watcher->result();
                             watcher->deleteLater();
                             // The dialog that asked may have closed while the frames were read.
                             if (dialogGuard)
                             {
                                 done(std::move(result.diagnostics), std::move(result.message));
                             }
                         });
        watcher->setFuture(QtConcurrent::run([request = std::move(request), session, demosaic]() {
            return runCometDialogSearch(session, demosaic, request,
                                        [](const an::ExecutionContext& execution,
                                           const an::ui::CometTrackingDialogBlindDetectRequest& admitted) {
                                            return an::ui::runCometTrackingDialogBlindDetect(execution, admitted);
                                        });
        }));
    });

    if (dialog->m_ui.autoAssignButton)
    {
        dialog->m_ui.autoAssignButton->setVisible(true);
        const QString machine_key_str = machine_key.toString();
        const QString machine_key_label = machine_key.toCompactString();

        QObject::disconnect(dialog->m_ui.autoAssignButton, nullptr, dialog, nullptr);
        QObject::connect(
            dialog->m_ui.autoAssignButton, &QPushButton::clicked, dialog,
            [this, dialog, machine_key_str, machine_key_label, group_key_struct]() {
                const auto groups_now = collectRegistrationGroups();
                auto group_it =
                    std::find_if(groups_now.begin(), groups_now.end(),
                                 [&](const RegistrationGroupEntry& entry) { return entry.key == group_key_struct; });
                if (group_it == groups_now.end() || group_it->frames.empty())
                {
                    return;
                }

                LOG_INFO("RegistrationTab: auto-reference requested (group='{}', frames={})",
                         machine_key_label.toStdString(), static_cast<int>(group_it->frames.size()));

                // If multiple filters are present, prompt user which filter to use.
                // For "All Filters", we compare candidates using filter-normalized scores.
                auto autoRefCtx = std::make_shared<AutoReferenceContext>();
                {
                    const auto light_frames = model->lightFrames().frames();
                    autoRefCtx->filterById.reserve(light_frames.size());
                    for (const auto& frame : light_frames)
                    {
                        const QString filterName = frame.metadata.filter().trimmed();
                        if (!filterName.isEmpty())
                        {
                            autoRefCtx->filterById.emplace(frame.id, filterName);
                        }
                    }
                }

                QSet<QString> uniqueFilters;
                for (const auto& id : group_it->frames)
                {
                    const auto itf = autoRefCtx->filterById.find(id);
                    if (itf != autoRefCtx->filterById.end())
                    {
                        uniqueFilters.insert(itf->second);
                    }
                }

                if (uniqueFilters.size() <= 1)
                {
                    continueAutoReference(dialog, group_key_struct, autoRefCtx, machine_key_label, machine_key_str);
                    return;
                }

                QStringList filters = uniqueFilters.values();
                filters.sort(Qt::CaseInsensitive);
                const QPointer<RegistrationSettingsDialog> dialogGuard(dialog);
                promptAutoReferenceChoice(
                    dialog, filters,
                    [this, dialogGuard, group_key_struct, autoRefCtx, machine_key_label,
                     machine_key_str](std::optional<AutoReferenceChoice> choice) {
                        if (!dialogGuard)
                        {
                            return;
                        }
                        if (!choice.has_value())
                        {
                            LOG_INFO("RegistrationTab: auto-reference cancelled by user (group='{}')",
                                     machine_key_label.toStdString());
                            return;
                        }

                        autoRefCtx->choice = std::move(choice);
                        const QString choice_text =
                            (autoRefCtx->choice->mode == AutoReferenceChoice::Mode::SingleFilter)
                                ? QStringLiteral("SingleFilter:%1").arg(autoRefCtx->choice->filterName)
                                : QStringLiteral("AllFilters");
                        LOG_INFO("RegistrationTab: auto-reference filter choice (group='{}', choice='{}')",
                                 machine_key_label.toStdString(), choice_text.toStdString());
                        continueAutoReference(dialogGuard.data(), group_key_struct, autoRefCtx, machine_key_label,
                                              machine_key_str);
                    });
            });
    }

    QObject::connect(dialog, &RegistrationSettingsDialog::applyToAllRequested, dialog,
                     [this, dialog]() { applySettingsToAllGroups(*dialog); });
    QObject::connect(dialog, &QDialog::accepted, q,
                     [this, dialog, machine_key]() { saveSettingsFromDialog(*dialog, machine_key); });
    dialog->open();
}

void RegistrationTab::Impl::continueAutoReference(RegistrationSettingsDialog* dialog,
                                                  const RegistrationGroupKey& group_key_struct,
                                                  const std::shared_ptr<AutoReferenceContext>& autoRefCtx,
                                                  const QString& machine_key_label, const QString& machine_key_str)
{
    const auto groups_now = collectRegistrationGroups();
    const auto group_it = std::find_if(groups_now.begin(), groups_now.end(), [&](const RegistrationGroupEntry& entry) {
        return entry.key == group_key_struct;
    });
    if (dialog == nullptr || group_it == groups_now.end() || group_it->frames.empty())
    {
        return;
    }

    const auto metrics_by_frame = loadRegistrationMetrics(*group_it);
    if (!lastDbError.isEmpty())
    {
        openNonBlockingMessageBox(q, QMessageBox::Warning, RegistrationTab::tr("Registration Metrics"),
                                  RegistrationTab::tr("Failed to read registration metrics:\n%1").arg(lastDbError));
        return;
    }

    const int total = static_cast<int>(group_it->frames.size());
    int available = 0;
    int succeeded = 0;
    for (const auto& id : group_it->frames)
    {
        const auto it2 = metrics_by_frame.find(id.canonicalPath());
        if (it2 == metrics_by_frame.end())
        {
            continue;
        }
        ++available;
        if (it2->second.metrics.succeeded)
        {
            ++succeeded;
        }
    }

    LOG_INFO("RegistrationTab: auto-reference metrics summary (group='{}', available={}/{}, "
             "succeeded={}/{})",
             machine_key_label.toStdString(), available, total, succeeded, available);

    if (available < total || available == 0 || succeeded < available)
    {
        LOG_INFO("RegistrationTab: auto-reference needs registration run (group='{}')",
                 machine_key_label.toStdString());
        const QString msg = RegistrationTab::tr("Registration metrics are missing or incomplete for this group.\n\n"
                                                "Available metrics: %1 / %2\n"
                                                "Succeeded: %3 / %4\n\n"
                                                "Run Registration now and then auto-detect the best reference frame?")
                                .arg(available)
                                .arg(total)
                                .arg(succeeded)
                                .arg(available);

        const QPointer<RegistrationSettingsDialog> dialogGuard(dialog);
        askWithoutBlocking(q, QMessageBox::Question, RegistrationTab::tr("Auto Reference"), msg,
                           QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes,
                           [this, dialogGuard, group_key_struct, autoRefCtx, machine_key_label,
                            machine_key_str](const QMessageBox::StandardButton reply) {
                               if (reply != QMessageBox::Yes)
                               {
                                   LOG_INFO("RegistrationTab: auto-reference user declined to run registration "
                                            "(group='{}')",
                                            machine_key_label.toStdString());
                                   return;
                               }
                               if (dialogGuard)
                               {
                                   runRegistrationForAutoReference(dialogGuard.data(), group_key_struct, autoRefCtx,
                                                                   machine_key_label, machine_key_str);
                               }
                           });
        return;
    }

    const auto best = selectBestReferenceFrameWithChoice(q, metrics_by_frame, group_it->frames, *autoRefCtx);
    if (!best.has_value())
    {
        LOG_WARNING("RegistrationTab: auto-reference failed to pick a frame (group='{}')",
                    machine_key_label.toStdString());
        openNonBlockingMessageBox(
            q, QMessageBox::Information, RegistrationTab::tr("Auto Reference"),
            RegistrationTab::tr("Unable to auto-detect a suitable reference frame from the available metrics."));
        return;
    }

    const auto itBest = metrics_by_frame.find(best->canonicalPath());
    const auto bestSucceeded = (itBest != metrics_by_frame.end()) ? itBest->second.metrics.succeeded : false;
    const auto bestScore = (itBest != metrics_by_frame.end()) ? frameQualityScore(itBest->second.metrics) : 0.0;
    LOG_INFO("RegistrationTab: auto-reference selected (group='{}', frame='{}', succeeded={}, score={})",
             machine_key_label.toStdString(), best->canonicalPath().toStdString(), bestSucceeded, bestScore);

    dialog->setReferenceImage(best->canonicalPath());
    openNonBlockingMessageBox(q, QMessageBox::Information, RegistrationTab::tr("Auto Reference"),
                              buildAutoReferenceResultText(*best, metrics_by_frame, group_it->frames, *autoRefCtx));
}

void RegistrationTab::Impl::runRegistrationForAutoReference(RegistrationSettingsDialog* dialog,
                                                            const RegistrationGroupKey& group_key_struct,
                                                            const std::shared_ptr<AutoReferenceContext>& autoRefCtx,
                                                            const QString& machine_key_label,
                                                            const QString& machine_key_str)
{
    if (model->outputDirectory().trimmed().isEmpty())
    {
        LOG_WARNING("RegistrationTab: auto-reference cannot run registration (group='{}', "
                    "reason='no output dir')",
                    machine_key_label.toStdString());
        openNonBlockingMessageBox(q, QMessageBox::Information, RegistrationTab::tr("Run Registration"),
                                  RegistrationTab::tr("Set an output directory before running registration."));
        return;
    }

    if (dialog->m_ui.autoAssignButton)
    {
        dialog->m_ui.autoAssignButton->setEnabled(false);
    }
    emit q->runRegistrationRequested(machine_key_str);
    LOG_INFO("RegistrationTab: runRegistrationRequested emitted (group='{}')", machine_key_label.toStdString());

    auto* timer = new QTimer(dialog);
    timer->setInterval(500);
    timer->setProperty("ticks", 0);

    QObject::connect(
        timer, &QTimer::timeout, dialog, [this, dialog, timer, group_key_struct, autoRefCtx, machine_key_label]() {
            const int ticks = timer->property("ticks").toInt() + 1;
            timer->setProperty("ticks", ticks);

            const auto groups_latest = collectRegistrationGroups();
            auto latest_it =
                std::find_if(groups_latest.begin(), groups_latest.end(),
                             [&](const RegistrationGroupEntry& entry) { return entry.key == group_key_struct; });
            if (latest_it == groups_latest.end() || latest_it->frames.empty())
            {
                timer->stop();
                timer->deleteLater();
                if (dialog->m_ui.autoAssignButton)
                    dialog->m_ui.autoAssignButton->setEnabled(true);
                return;
            }

            const auto latest_metrics = loadRegistrationMetrics(*latest_it);
            if (!lastDbError.isEmpty())
            {
                // Keep waiting; DB may still be locked/being written.
                if (ticks > 120)
                {
                    timer->stop();
                    timer->deleteLater();
                    openNonBlockingMessageBox(
                        q, QMessageBox::Warning, RegistrationTab::tr("Auto Reference"),
                        RegistrationTab::tr("Timed out waiting for registration metrics.\n%1").arg(lastDbError));
                    if (dialog->m_ui.autoAssignButton)
                        dialog->m_ui.autoAssignButton->setEnabled(true);
                }
                return;
            }

            int avail2 = 0;
            int succ2 = 0;
            for (const auto& id : latest_it->frames)
            {
                const auto it3 = latest_metrics.find(id.canonicalPath());
                if (it3 == latest_metrics.end())
                {
                    continue;
                }
                ++avail2;
                if (it3->second.metrics.succeeded)
                {
                    ++succ2;
                }
            }

            if (avail2 < static_cast<int>(latest_it->frames.size()) || succ2 < avail2)
            {
                if (ticks > 120)
                {
                    LOG_WARNING("RegistrationTab: auto-reference timed out waiting for complete "
                                "metrics (group='{}', available={}/{}, succeeded={}/{})",
                                machine_key_label.toStdString(), avail2, static_cast<int>(latest_it->frames.size()),
                                succ2, avail2);
                    timer->stop();
                    timer->deleteLater();
                    openNonBlockingMessageBox(
                        q, QMessageBox::Warning, RegistrationTab::tr("Auto Reference"),
                        RegistrationTab::tr("Registration finished, but metrics are still incomplete.\n"
                                            "Please review failed frames and run registration again."));
                    if (dialog->m_ui.autoAssignButton)
                        dialog->m_ui.autoAssignButton->setEnabled(true);
                }
                return;
            }

            const auto best = selectBestReferenceFrameWithChoice(q, latest_metrics, latest_it->frames, *autoRefCtx);
            if (best.has_value())
            {
                const auto itBest = latest_metrics.find(best->canonicalPath());
                const auto bestSucceeded = (itBest != latest_metrics.end()) ? itBest->second.metrics.succeeded : false;
                const auto bestScore =
                    (itBest != latest_metrics.end()) ? frameQualityScore(itBest->second.metrics) : 0.0;
                LOG_INFO("RegistrationTab: auto-reference selected after registration (group='{}', "
                         "frame='{}', succeeded={}, score={})",
                         machine_key_label.toStdString(), best->canonicalPath().toStdString(), bestSucceeded,
                         bestScore);
                dialog->setReferenceImage(best->canonicalPath());
                openNonBlockingMessageBox(
                    q, QMessageBox::Information, RegistrationTab::tr("Auto Reference"),
                    buildAutoReferenceResultText(*best, latest_metrics, latest_it->frames, *autoRefCtx));
            }

            timer->stop();
            timer->deleteLater();
            if (dialog->m_ui.autoAssignButton)
                dialog->m_ui.autoAssignButton->setEnabled(true);
        });

    timer->start();
}

void RegistrationTab::Impl::saveSettingsFromDialog(const RegistrationSettingsDialog& dialog,
                                                   const GroupKey& machine_key)
{
    const auto apply_from_dialog = [&](RegistrationSettings& settings, const bool include_reference) {
        settings.minStarPairs = dialog.getMinStarPairs();
        settings.maxStarPairs = dialog.getMaxStarPairs();
        settings.autoReferenceDuringRegistration = dialog.getAutoReferenceDuringRegistration();
        settings.autoSolveUnsolvedFrames = dialog.getAutoSolveUnsolvedFrames();
        settings.enrichWithGaiaCatalog = dialog.getEnrichWithGaiaCatalog();
        if (include_reference)
        {
            settings.referenceImage = dialog.getReferenceImage();
        }
        settings.registrationMethod = dialog.getRegistrationMethod();
        settings.transformation = dialog.getTransformation();
        settings.transformMode = dialog.getTransformMode();
        settings.transformOrder = dialog.getTransformOrder();
        settings.twoPassRefinementEnabled = dialog.getTwoPassRefinementEnabled();
        settings.alignmentMode = dialog.getAlignmentMode();
        settings.interpolation = dialog.getInterpolation();
        settings.manualControlPointsByFrame = dialog.getManualControlPointsByFrame();
        settings.cometTracking = dialog.getCometTrackingSettings();
        settings.chromaticAberration = dialog.getChromaticAberrationSettings();
        settings.radius = dialog.getRadius();
        settings.threshold = dialog.getThreshold();
        settings.amplitudeRangeEnabled = dialog.getAmplitudeRangeEnabled();
        settings.thresholdMinimum = dialog.getThresholdMinimum();
        settings.thresholdMaximum = dialog.getThresholdMaximum();
        settings.roundnessThreshold = dialog.getRoundnessThreshold();
        settings.roundnessRangeEnabled = dialog.getRoundnessRangeEnabled();
        settings.roundnessThresholdMaximum = dialog.getRoundnessThresholdMaximum();
        settings.convergence = dialog.getConvergence();
        settings.relaxPsfChecks = dialog.getRelaxPSFChecks();
        settings.profileType = dialog.getProfileType();
        settings.minimumBeta = dialog.getMinimumBeta();
        settings.cfaPattern = dialog.getCFAPattern();
        settings.debayerMethod = dialog.getDebayerMethod();
    };

    RegistrationSettings settings;
    if (auto it = model->registrationSettings().find(machine_key); it != model->registrationSettings().end())
    {
        settings = it->second;
    }

    apply_from_dialog(settings, true);
    if (undoStack)
    {
        const RegistrationSettings oldSettings = settingsForGroup(machine_key);
        undoStack->push(new SetRegistrationSettingsCommand(model, machine_key, oldSettings, settings));
    }
    else
    {
        model->setRegistrationSettings(machine_key, settings);
    }
}

void RegistrationTab::Impl::applySettingsToAllGroups(const RegistrationSettingsDialog& dialog)
{
    if (ImageIntegration::RegistrationSettingsUtils::normalizeRegistrationMethod(dialog.getRegistrationMethod()) ==
        QStringLiteral("comet_tracking"))
    {
        return;
    }

    if (undoStack)
    {
        model->beginBatchUpdate();
        undoStack->beginMacro(RegistrationTab::tr("Update registration settings"));
    }
    else
    {
        model->beginBatchUpdate();
    }
    for (const auto& group : cachedGroups)
    {
        const GroupKey machine_key = RegistrationGroupEngine::toMachineGroupKey(group.key);

        RegistrationSettings settings;
        if (auto it = model->registrationSettings().find(machine_key); it != model->registrationSettings().end())
        {
            settings = it->second;
        }

        if (ImageIntegration::RegistrationSettingsUtils::usesCometTracking(settings.registrationMethod))
        {
            continue;
        }

        // Do not copy reference frames across groups (references are group-specific).
        settings.minStarPairs = dialog.getMinStarPairs();
        settings.maxStarPairs = dialog.getMaxStarPairs();
        settings.autoReferenceDuringRegistration = dialog.getAutoReferenceDuringRegistration();
        settings.autoSolveUnsolvedFrames = dialog.getAutoSolveUnsolvedFrames();
        settings.enrichWithGaiaCatalog = dialog.getEnrichWithGaiaCatalog();
        settings.registrationMethod = dialog.getRegistrationMethod();
        settings.transformation = dialog.getTransformation();
        settings.transformMode = dialog.getTransformMode();
        settings.transformOrder = dialog.getTransformOrder();
        settings.twoPassRefinementEnabled = dialog.getTwoPassRefinementEnabled();
        settings.alignmentMode = dialog.getAlignmentMode();
        settings.interpolation = dialog.getInterpolation();
        settings.chromaticAberration = dialog.getChromaticAberrationSettings();
        settings.radius = dialog.getRadius();
        settings.threshold = dialog.getThreshold();
        settings.amplitudeRangeEnabled = dialog.getAmplitudeRangeEnabled();
        settings.thresholdMinimum = dialog.getThresholdMinimum();
        settings.thresholdMaximum = dialog.getThresholdMaximum();
        settings.roundnessThreshold = dialog.getRoundnessThreshold();
        settings.roundnessRangeEnabled = dialog.getRoundnessRangeEnabled();
        settings.roundnessThresholdMaximum = dialog.getRoundnessThresholdMaximum();
        settings.convergence = dialog.getConvergence();
        settings.relaxPsfChecks = dialog.getRelaxPSFChecks();
        settings.profileType = dialog.getProfileType();
        settings.minimumBeta = dialog.getMinimumBeta();
        settings.cfaPattern = dialog.getCFAPattern();
        settings.debayerMethod = dialog.getDebayerMethod();

        if (undoStack)
        {
            const RegistrationSettings oldSettings = settingsForGroup(machine_key);
            undoStack->push(new SetRegistrationSettingsCommand(model, machine_key, oldSettings, settings));
        }
        else
        {
            model->setRegistrationSettings(machine_key, settings);
        }
    }
    if (undoStack)
    {
        undoStack->endMacro();
        model->endBatchUpdate();
    }
    else
    {
        model->endBatchUpdate();
    }
}

void RegistrationTab::Impl::applyQualityThresholds()
{
    const RegistrationGroupEntry* group = selectedGroup();
    if (!group)
    {
        return;
    }

    const QualityThresholdConfig thresholds = currentQualityThresholdConfig();

    if (!thresholds.anyEnabled())
    {
        openNonBlockingMessageBox(q, QMessageBox::Information, RegistrationTab::tr("No Filters Set"),
                                  RegistrationTab::tr("Please enable at least one quality filter."));
        return;
    }

    const FrameScoreContext scoreContext = buildFrameScoreContext(*group);
    int excluded_count = 0;
    std::vector<FrameAttributeChange> changes;
    changes.reserve(group->frames.size());

    for (const auto& frame_id : group->frames)
    {
        const auto metrics = model->frameQualityMetrics(frame_id);
        if (!metrics.has_value() || !metrics->succeeded)
        {
            continue;
        }

        const double score = qualityScoreForFrame(frame_id, *metrics, scoreContext);
        const bool should_exclude = frameFailsQualityThresholds(*metrics, score, thresholds);

        if (should_exclude)
        {
            const QString path = frame_id.canonicalPath();
            const auto frameOpt = model->frames(FrameType::Light).findById(FrameId(path));
            if (!frameOpt.has_value() || frameOpt->excluded)
            {
                continue;
            }
            changes.push_back(FrameAttributeChange{path, false, true});
            ++excluded_count;
        }
    }
    if (!changes.empty())
    {
        if (undoStack)
        {
            undoStack->push(new SetFrameAttributesCommand(model, FrameAttribute::Excluded, std::move(changes)));
        }
        else
        {
            model->beginBatchUpdate();
            for (const auto& change : changes)
            {
                model->updateFrameAttribute(change.path, FrameAttribute::Excluded, true);
            }
            model->endBatchUpdate();
        }
    }

    if (excluded_count > 0)
    {
        openNonBlockingMessageBox(
            q, QMessageBox::Information, RegistrationTab::tr("Filters Applied"),
            RegistrationTab::tr("Excluded %1 frame(s) based on quality filters.").arg(excluded_count));
    }
    else
    {
        openNonBlockingMessageBox(
            q, QMessageBox::Information, RegistrationTab::tr("Filters Applied"),
            RegistrationTab::tr("No frames were excluded. All frames meet the enabled quality filters."));
    }
}

void RegistrationTab::Impl::detectOutliers()
{
    const RegistrationGroupEntry* group = selectedGroup();
    if (!group)
    {
        return;
    }

    const FrameScoreContext scoreContext = buildFrameScoreContext(*group);

    std::vector<double> scores;
    std::vector<double> stars;
    std::vector<double> snr;
    std::vector<double> fwhm;
    std::vector<double> ecc;
    scores.reserve(group->frames.size());
    stars.reserve(group->frames.size());
    snr.reserve(group->frames.size());
    fwhm.reserve(group->frames.size());
    ecc.reserve(group->frames.size());

    for (const auto& frame_id : group->frames)
    {
        const auto metrics = model->frameQualityMetrics(frame_id);
        if (!metrics.has_value() || !metrics->succeeded)
        {
            continue;
        }
        scores.push_back(qualityScoreForFrame(frame_id, *metrics, scoreContext));
        stars.push_back(static_cast<double>(metrics->stars));
        snr.push_back(metrics->snr);
        fwhm.push_back(metrics->fwhm);
        ecc.push_back(metrics->eccentricity);
    }

    if (scores.empty() && stars.empty() && snr.empty() && fwhm.empty() && ecc.empty())
    {
        openNonBlockingMessageBox(q, QMessageBox::Information, RegistrationTab::tr("No Metrics Available"),
                                  RegistrationTab::tr("No frame quality metrics found for the selected group."));
        return;
    }

    const SuggestedThresholds suggested = suggestThresholdsFromIqr(scores, stars, snr, fwhm, ecc);

    QStringList lines;
    lines << RegistrationTab::tr("Suggested quality filters (IQR-based):");
    lines << RegistrationTab::tr("- Min Score: %1").arg(suggested.minScore, 0, 'f', 2);
    lines << RegistrationTab::tr("- Min Stars: %1").arg(suggested.minStars);
    lines << RegistrationTab::tr("- Min SNR: %1").arg(suggested.minSnr, 0, 'f', 2);
    lines << RegistrationTab::tr("- Max FWHM: %1").arg(suggested.maxFwhm, 0, 'f', 2);
    lines << RegistrationTab::tr("- Max Ecc: %1").arg(suggested.maxEcc, 0, 'f', 2);

    // Asked without a nested event loop; the suggestion is applied once the person answers.
    askWithoutBlocking(
        q, QMessageBox::Question, RegistrationTab::tr("Detect Outliers"),
        lines.join("\n") + "\n\n" + RegistrationTab::tr("Set these values into the quality filter controls?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes,
        [this, suggested](const QMessageBox::StandardButton reply) {
            if (reply != QMessageBox::Yes)
            {
                return;
            }

            if (ui.spinMinScore)
            {
                ui.spinMinScore->setValue(std::clamp(suggested.minScore, 0.0, ui.spinMinScore->maximum()));
            }
            if (ui.chkMinScore)
            {
                ui.chkMinScore->setChecked(suggested.minScore > 0.0);
            }
            if (ui.spinMinStars)
            {
                ui.spinMinStars->setValue(suggested.minStars);
            }
            if (ui.chkMinStars)
            {
                ui.chkMinStars->setChecked(suggested.minStars > 0);
            }
            if (ui.spinMinSnr)
            {
                ui.spinMinSnr->setValue(std::clamp(suggested.minSnr, 0.0, ui.spinMinSnr->maximum()));
            }
            if (ui.chkMinSnr)
            {
                ui.chkMinSnr->setChecked(suggested.minSnr > 0.0);
            }
            if (ui.spinMaxFwhm)
            {
                ui.spinMaxFwhm->setValue(std::clamp(suggested.maxFwhm, 0.0, ui.spinMaxFwhm->maximum()));
            }
            if (ui.chkMaxFwhm)
            {
                ui.chkMaxFwhm->setChecked(suggested.maxFwhm > 0.0);
            }
            if (ui.spinMaxEccentricity)
            {
                ui.spinMaxEccentricity->setValue(std::clamp(suggested.maxEcc, 0.0, ui.spinMaxEccentricity->maximum()));
            }
            if (ui.chkMaxEccentricity)
            {
                ui.chkMaxEccentricity->setChecked(suggested.maxEcc > 0.0);
            }

            syncThresholdEditorStates();
            updateThresholdPreview();
            updateChartThresholds();

            askWithoutBlocking(q, QMessageBox::Question, RegistrationTab::tr("Apply Filters"),
                               RegistrationTab::tr("Apply filters now and exclude outlier frames?"),
                               QMessageBox::Yes | QMessageBox::No, QMessageBox::No,
                               [this](const QMessageBox::StandardButton applyReply) {
                                   if (applyReply == QMessageBox::Yes)
                                   {
                                       applyQualityThresholds();
                                   }
                               });
        });
}

QualityThresholdConfig RegistrationTab::Impl::currentQualityThresholdConfig() const
{
    QualityThresholdConfig thresholds;
    thresholds.minScoreEnabled = ui.chkMinScore && ui.chkMinScore->isChecked();
    thresholds.minScore = ui.spinMinScore ? ui.spinMinScore->value() : 0.0;
    thresholds.minStarsEnabled = ui.chkMinStars && ui.chkMinStars->isChecked();
    thresholds.minStars = ui.spinMinStars ? ui.spinMinStars->value() : 0;
    thresholds.minSnrEnabled = ui.chkMinSnr && ui.chkMinSnr->isChecked();
    thresholds.minSnr = ui.spinMinSnr ? ui.spinMinSnr->value() : 0.0;
    thresholds.maxFwhmEnabled = ui.chkMaxFwhm && ui.chkMaxFwhm->isChecked();
    thresholds.maxFwhm = ui.spinMaxFwhm ? ui.spinMaxFwhm->value() : 0.0;
    thresholds.maxEccEnabled = ui.chkMaxEccentricity && ui.chkMaxEccentricity->isChecked();
    thresholds.maxEcc = ui.spinMaxEccentricity ? ui.spinMaxEccentricity->value() : 0.0;
    return thresholds;
}

void RegistrationTab::Impl::syncThresholdEditorStates()
{
    const auto syncEditor = [](const QCheckBox* checkBox, QWidget* editor) {
        if (editor)
        {
            editor->setEnabled(checkBox && checkBox->isChecked());
        }
    };

    syncEditor(ui.chkMinScore, ui.spinMinScore);
    syncEditor(ui.chkMinStars, ui.spinMinStars);
    syncEditor(ui.chkMinSnr, ui.spinMinSnr);
    syncEditor(ui.chkMaxFwhm, ui.spinMaxFwhm);
    syncEditor(ui.chkMaxEccentricity, ui.spinMaxEccentricity);
}

void RegistrationTab::Impl::updateChartThresholds()
{
    if (!chartWidget)
    {
        return;
    }

    const QualityThresholdConfig thresholds = currentQualityThresholdConfig();
    chartWidget->setMinScoreThreshold(thresholds.minScoreEnabled ? thresholds.minScore : 0.0);
    chartWidget->setMinStarsThreshold(thresholds.minStarsEnabled ? thresholds.minStars : 0);
    chartWidget->setMinSnrThreshold(thresholds.minSnrEnabled ? thresholds.minSnr : 0.0);
    chartWidget->setMaxFwhmThreshold(thresholds.maxFwhmEnabled ? thresholds.maxFwhm : 0.0);
    chartWidget->setMaxEccentricityThreshold(thresholds.maxEccEnabled ? thresholds.maxEcc : 0.0);
}

std::optional<FrameId> RegistrationTab::Impl::selectedSingleFrameId() const
{
    if (!ui.tblFrameQuality)
    {
        return std::nullopt;
    }

    const auto selected =
        ui.tblFrameQuality->selectionModel() ? ui.tblFrameQuality->selectionModel()->selectedRows() : QModelIndexList();
    if (selected.size() != 1)
    {
        return std::nullopt;
    }

    const int row = selected.front().row();
    auto* item = ui.tblFrameQuality->item(row, kFrameCol);
    const QString canonical = item ? item->data(Qt::UserRole).toString().trimmed() : QString();
    if (canonical.isEmpty())
    {
        return std::nullopt;
    }

    const FrameId id(canonical);
    if (auto info = model->lightFrames().findById(id); info.has_value() && info->excluded)
    {
        return std::nullopt;
    }

    return id;
}

RegistrationTab::Impl::SelectedFramePartition RegistrationTab::Impl::selectedFramePathsByExclusion() const
{
    SelectedFramePartition out;
    if (!ui.tblFrameQuality || !ui.tblFrameQuality->selectionModel())
    {
        return out;
    }

    const auto selectedRows = ui.tblFrameQuality->selectionModel()->selectedRows();
    QSet<QString> seen;
    for (const auto& idx : selectedRows)
    {
        const int row = idx.row();
        auto* item = ui.tblFrameQuality->item(row, kFrameCol);
        const QString canonical = item ? item->data(Qt::UserRole).toString().trimmed() : QString();
        if (canonical.isEmpty() || seen.contains(canonical))
        {
            continue;
        }
        seen.insert(canonical);

        bool isExcluded = false;
        if (auto info = model->lightFrames().findById(FrameId(canonical)); info.has_value())
        {
            isExcluded = info->excluded;
        }

        (isExcluded ? out.excluded : out.included).append(canonical);
    }

    return out;
}

void RegistrationTab::Impl::setFramesExcluded(const QStringList& canonicalPaths, bool excluded)
{
    if (canonicalPaths.isEmpty())
    {
        return;
    }

    std::vector<FrameAttributeChange> changes;
    changes.reserve(canonicalPaths.size());
    for (const auto& path : canonicalPaths)
    {
        const auto frameOpt = model->frames(FrameType::Light).findById(FrameId(path));
        if (!frameOpt.has_value())
        {
            continue;
        }
        if (frameOpt->excluded == excluded)
        {
            continue;
        }
        changes.push_back(FrameAttributeChange{path, frameOpt->excluded, excluded});
    }

    if (changes.empty())
    {
        return;
    }

    if (undoStack)
    {
        undoStack->push(new SetFrameAttributesCommand(model, FrameAttribute::Excluded, std::move(changes)));
    }
    else
    {
        model->setFramesExcluded(FrameType::Light, canonicalPaths, excluded);
    }
}

void RegistrationTab::Impl::resetExcludedFrames()
{
    const RegistrationGroupEntry* group = selectedGroup();
    if (!group || group->frames.empty())
    {
        return;
    }

    // Collect all excluded frames in this group
    QStringList excludedPaths;
    for (const auto& frame : model->lightFrames().frames())
    {
        if (!frame.excluded)
        {
            continue;
        }

        const RegistrationGroupKey key = RegistrationGroupKey::fromFrame(frame);
        if (key == group->key)
        {
            excludedPaths.append(frame.id.canonicalPath());
        }
    }

    if (excludedPaths.isEmpty())
    {
        openNonBlockingMessageBox(q, QMessageBox::Information, RegistrationTab::tr("Reset Excluded"),
                                  RegistrationTab::tr("No excluded frames in this group."));
        return;
    }

    askWithoutBlocking(q, QMessageBox::Question, RegistrationTab::tr("Reset Excluded Frames"),
                       RegistrationTab::tr("This will re-include %n excluded frame(s) in this registration group.\n\n"
                                           "These frames will be used in registration and stacking again.\n\n"
                                           "Continue?",
                                           nullptr, excludedPaths.size()),
                       QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes,
                       [this, excludedPaths](const QMessageBox::StandardButton reply) {
                           if (reply != QMessageBox::Yes)
                           {
                               return;
                           }

                           setFramesExcluded(excludedPaths, false);

                           openNonBlockingMessageBox(
                               q, QMessageBox::Information, RegistrationTab::tr("Reset Excluded"),
                               RegistrationTab::tr("Re-included %n frame(s).", nullptr, excludedPaths.size()));
                       });
}

void RegistrationTab::Impl::clearRegistrationForSelectedGroup()
{
    const RegistrationGroupEntry* selected = selectedGroup();
    if (!selected)
    {
        return;
    }

    const QString output_dir = model->outputDirectory().trimmed();
    if (output_dir.isEmpty())
    {
        openNonBlockingMessageBox(q, QMessageBox::Information, RegistrationTab::tr("Clear Registration"),
                                  RegistrationTab::tr("Set an output directory before clearing registration data."));
        return;
    }

    // The group is copied: the cached groups may be rebuilt while the person answers.
    const RegistrationGroupEntry group = *selected;
    askWithoutBlocking(
        q, QMessageBox::Warning, RegistrationTab::tr("Clear Registration"),
        RegistrationTab::tr("This will delete star detection cache and registration metrics for the selected group.\n\n"
                            "Deleted cache files cannot be restored.\n\n"
                            "Continue?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No,
        [this, group, output_dir](const QMessageBox::StandardButton reply) {
            if (reply != QMessageBox::Yes)
            {
                return;
            }

            const QString group_key = RegistrationGroupEngine::toMachineGroupKey(group.key).toString();
            const QString metrics_db_path = QDir(output_dir).filePath(QStringLiteral("registration_metrics.db"));
            if (!QFileInfo::exists(metrics_db_path))
            {
                openNonBlockingMessageBox(
                    q, QMessageBox::Information, RegistrationTab::tr("Clear Registration"),
                    RegistrationTab::tr("No registration database was found for the current output directory."));
                return;
            }

            QString deleteError;
            closeMetricsDb();

            // Records of another format are refused, not written; the refusal is said as the failure to clear.
            an::ui::RegistrationMetricsWriter writer(metrics_db_path, QStringLiteral("RegistrationTabClearMetrics"));
            const bool initialized = writer.initialize(&deleteError);
            const bool cleared = initialized && writer.deleteAllForGroup(group_key, &deleteError);

            if (!cleared)
            {
                openNonBlockingMessageBox(
                    q, QMessageBox::Warning, RegistrationTab::tr("Clear Registration"),
                    RegistrationTab::tr("Failed to clear registration data: %1").arg(deleteError));
                LOG_WARNING("RegistrationTab: failed to clear registration data (group='{}'): {}",
                            group_key.toStdString(), deleteError.toStdString());
                return;
            }

            if (undoStack)
            {
                std::vector<std::pair<FrameId, FrameQualityMetrics>> oldMetrics;
                oldMetrics.reserve(group.frames.size());
                for (const auto& id : group.frames)
                {
                    if (const auto metrics = model->frameQualityMetrics(id); metrics.has_value())
                    {
                        oldMetrics.emplace_back(id, *metrics);
                    }
                }

                undoStack->push(new ClearFrameQualityMetricsCommand(model, group.frames, std::move(oldMetrics)));
            }
            else
            {
                model->clearFrameQualityMetrics(group.frames);
            }

            LOG_INFO("RegistrationTab: cleared registration data (group='{}')", group_key.toStdString());
            updateAll();
        });
}

bool RegistrationTab::Impl::groupNeedsRegistrationWork(const RegistrationGroupEntry& selectedGroup,
                                                       QStringList& reasons_out)
{
    reasons_out.clear();

    const QString output_dir = model->outputDirectory().trimmed();
    if (output_dir.isEmpty())
    {
        reasons_out << RegistrationTab::tr("Output directory is not set.");
        return true;
    }

    const GroupKey machine_key = RegistrationGroupEngine::toMachineGroupKey(selectedGroup.key);
    const QString group_key = machine_key.toString();

    const WorkflowManifest* analysis_manifest = currentRegistrationAnalysisManifest();
    if (analysis_manifest == nullptr)
    {
        reasons_out << registrationAnalysisManifestError;
        return true;
    }

    const auto manifest_group =
        std::find_if(analysis_manifest->registrationGroups.cbegin(), analysis_manifest->registrationGroups.cend(),
                     [&](const WorkflowManifest::RegistrationGroup& group) { return group.groupKey == machine_key; });
    if (manifest_group == analysis_manifest->registrationGroups.cend())
    {
        const bool has_included_light =
            std::any_of(selectedGroup.frames.cbegin(), selectedGroup.frames.cend(), [&](const FrameId& frame) {
                const auto info = model->lightFrames().findById(frame);
                return info.has_value() && !info->excluded;
            });
        if (!has_included_light)
        {
            reasons_out << RegistrationTab::tr("No included light frames remain in this group.");
            return false;
        }
        reasons_out << RegistrationTab::tr("The selected registration group is not present in current workflow facts.");
        return true;
    }

    bool resolved = false;
    bool needsWork = true;
    const bool executed = executeMetricsDb([&](QSqlDatabase& db) {
        resolved = true;

        const RegistrationWorkAnalysis analysis = analyzeRegistrationWork(db, *analysis_manifest, *manifest_group);
        reasons_out.reserve(static_cast<int>(analysis.reasons.size()));
        for (const auto& reason : analysis.reasons)
        {
            reasons_out << translatedRegistrationWorkReason(reason);
        }
        needsWork = analysis.needsWork;
    });

    if (!executed || !resolved)
    {
        const QString accessError = metricsDataAccess.lastError().trimmed();
        if (!accessError.isEmpty())
        {
            reasons_out << RegistrationTab::tr("Metrics database unavailable: %1").arg(accessError);
        }
        else
        {
            reasons_out << RegistrationTab::tr("No previous registration run found (metrics database missing).");
        }
        return true;
    }

    return needsWork;
}

void RegistrationTab::Impl::updateRunAllButton()
{
    if (!ui.btnRunRegistration)
    {
        return;
    }

    const bool has_output = !model->outputDirectory().trimmed().isEmpty();
    const bool has_groups = std::any_of(cachedGroups.begin(), cachedGroups.end(),
                                        [](const RegistrationGroupEntry& g) { return !g.frames.empty(); });
    ui.btnRunRegistration->setEnabled(has_output && has_groups);
}

void RegistrationTab::Impl::updateRunGroupButton(const RegistrationGroupEntry* group)
{
    if (!btnRunRegistrationGroup)
    {
        return;
    }

    const bool has_output = !model->outputDirectory().trimmed().isEmpty();
    const bool has_group = (group != nullptr) && !group->frames.empty();

    QString tooltip = RegistrationTab::tr("Run the registration workflow for the selected registration group only.\n"
                                          "Only new/changed frames are processed when possible.\n\n");

    bool enabled = false;
    if (!has_group)
    {
        tooltip += RegistrationTab::tr("Disabled: select a registration group.");
    }
    else if (!has_output)
    {
        tooltip += RegistrationTab::tr("Disabled: set an output directory first.");
    }
    else
    {
        QStringList reasons;
        const bool has_work = groupNeedsRegistrationWork(*group, reasons);
        enabled = has_work;
        if (has_work)
        {
            tooltip += RegistrationTab::tr("Enabled because:\n• %1").arg(reasons.join(QStringLiteral("\n• ")));
        }
        else
        {
            tooltip += reasons.isEmpty()
                           ? RegistrationTab::tr("Disabled: no changes detected since the last run.")
                           : RegistrationTab::tr("Disabled: %1").arg(reasons.join(QStringLiteral("\n• ")));
        }
    }

    btnRunRegistrationGroup->setEnabled(enabled);
    btnRunRegistrationGroup->setToolTip(tooltip);
}

void RegistrationTab::Impl::updateClearGroupButton(const RegistrationGroupEntry* group)
{
    if (!btnClearRegistrationGroup)
    {
        return;
    }

    bool enabled = false;
    QString tooltip = RegistrationTab::tr(
        "Remove all registration data for the selected group (star detection cache and metrics).\n\n");

    if (!group || group->frames.empty())
    {
        tooltip += RegistrationTab::tr("Disabled: select a registration group.");
    }
    else
    {
        bool queried = false;
        bool has_metrics = false;
        bool has_stars = false;
        bool has_state = false;
        bool has_comet_state = false;
        const QString group_key = RegistrationGroupEngine::toMachineGroupKey(group->key).toString();

        const bool executed = executeMetricsDb([&](QSqlDatabase& db) {
            queried = true;

            const auto tableExists = [&](const QString& table_name) -> bool {
                QSqlQuery table_query(db);
                table_query.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = "
                                                   ":table_name LIMIT 1"));
                table_query.bindValue(QStringLiteral(":table_name"), table_name);
                return table_query.exec() && table_query.next();
            };

            QSqlQuery q1(db);
            q1.prepare(QStringLiteral("SELECT 1 FROM registration_metrics WHERE group_key = :group_key LIMIT 1"));
            q1.bindValue(QStringLiteral(":group_key"), group_key);
            has_metrics = q1.exec() && q1.next();

            QSqlQuery q2(db);
            q2.prepare(QStringLiteral("SELECT 1 FROM star_detection_cache WHERE group_key = :group_key LIMIT 1"));
            q2.bindValue(QStringLiteral(":group_key"), group_key);
            has_stars = q2.exec() && q2.next();

            QSqlQuery q3(db);
            q3.prepare(QStringLiteral("SELECT 1 FROM registration_group_state WHERE group_key = :group_key LIMIT 1"));
            q3.bindValue(QStringLiteral(":group_key"), group_key);
            has_state = q3.exec() && q3.next();

            if (tableExists(QStringLiteral("comet_tracking_runs")))
            {
                QSqlQuery q4(db);
                q4.prepare(QStringLiteral("SELECT 1 FROM comet_tracking_runs WHERE group_key = :group_key LIMIT 1"));
                q4.bindValue(QStringLiteral(":group_key"), group_key);
                has_comet_state = q4.exec() && q4.next();
            }
        });

        if (!executed || !queried)
        {
            tooltip += RegistrationTab::tr("Disabled: no metrics database found.");
        }
        else
        {
            enabled = has_metrics || has_stars || has_state || has_comet_state;
            if (!enabled)
            {
                tooltip += RegistrationTab::tr("Disabled: no registration data found for this group.");
            }
            else
            {
                tooltip += RegistrationTab::tr("Enabled: will delete cached stars and metrics for this group.");
            }
        }
    }

    btnClearRegistrationGroup->setEnabled(enabled);
    btnClearRegistrationGroup->setToolTip(tooltip);
}

void RegistrationTab::Impl::updateResetExcludedButton(const RegistrationGroupEntry* group)
{
    if (!ui.btnResetExcluded)
    {
        return;
    }

    // Count excluded frames in the current group
    cachedExcludedCount = 0;
    if (group)
    {
        for (const auto& frame : model->lightFrames().frames())
        {
            if (!frame.excluded)
            {
                continue;
            }

            const RegistrationGroupKey key = RegistrationGroupKey::fromFrame(frame);
            if (key == group->key)
            {
                ++cachedExcludedCount;
            }
        }
    }

    const bool enabled = cachedExcludedCount > 0;
    ui.btnResetExcluded->setEnabled(enabled);

    // Build human-friendly tooltip
    QString tooltip;
    if (!group || group->frames.empty())
    {
        tooltip = RegistrationTab::tr("Re-include all excluded frames in this registration group.\n\n"
                                      "No registration group is currently selected.");
    }
    else if (cachedExcludedCount == 0)
    {
        tooltip = RegistrationTab::tr("Re-include all excluded frames in this registration group.\n\n"
                                      "Currently disabled: no frames are excluded in this group.");
    }
    else
    {
        tooltip =
            RegistrationTab::tr("Re-include all %n excluded frame(s) in this registration group.\n\n"
                                "Click to restore these frames so they will be used in registration and stacking.",
                                nullptr, cachedExcludedCount);
    }
    ui.btnResetExcluded->setToolTip(tooltip);
}

void RegistrationTab::Impl::updateActionButtons()
{
    const RegistrationGroupEntry* group = selectedGroup();
    const auto selected = selectedFramePathsByExclusion();

    if (ui.btnSetReference)
    {
        ui.btnSetReference->setEnabled(selectedSingleFrameId().has_value());
    }

    if (ui.btnExcludeSelected)
    {
        ui.btnExcludeSelected->setEnabled(!selected.included.isEmpty());
    }

    if (ui.btnIncludeSelected)
    {
        ui.btnIncludeSelected->setEnabled(!selected.excluded.isEmpty());
    }

    if (ui.btnGroupSettings)
    {
        const bool has_group = (group != nullptr) && !group->frames.empty();
        ui.btnGroupSettings->setEnabled(has_group);
    }

    updateRunAllButton();
    updateRunGroupButton(group);
    updateClearGroupButton(group);
    updateResetExcludedButton(group);

    updateThresholdPreview();
}
