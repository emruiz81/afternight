// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImageIntegrationProcess.h"

#include "app/ApplicationStorage.h"
#include "app/DesktopUtils.h"
#include "app/FileDialogUtils.h"
#include "controls/CalibrationPreviewConfig.h"
#include "controls/ImageViewControl.h"
#include "components/AnProgressCard.h"
#include "processes/ProcessCommonUtils.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/CalibrationMatcher.h"
#include "processes/ImageIntegration/model/FrameQuality.h"
#include "processes/ImageIntegration/ui_models/GroupTreeModel.h"
#include "processes/ImageIntegration/widgets/FrameTreeView.h"
#include "processes/ImageIntegration/widgets/TreeContextMenuHandler.h"
#include "processes/ImageIntegration/widgets/FrameTreeDragDropHandler.h"
#include "processes/ImageIntegration/dialogs/BlinkComparatorDialog.h"
#include "processes/ImageIntegration/dialogs/ConfirmationDialogUtils.h"
#include "processes/ImageIntegration/tabs/CalibrationTab.h"
#include "processes/ImageIntegration/tabs/CalibrationPanelUtils.h"
#include "processes/ImageIntegration/tabs/FrameSourceTab.h"
#include "processes/ImageIntegration/tabs/RegistrationTab.h"
#include "processes/ImageIntegration/tabs/StackingTab.h"
#include "processes/ImageIntegration/tabs/SummaryTab.h"
#include "processes/ImageIntegration/widgets/TabIssueBadge.h"
#include "processes/ImageIntegration/workflow/ManifestBuilder.h"
#include "processes/ImageIntegration/workflow/ExecutionPlanner.h"
#include "processes/ImageIntegration/workflow/ExecutionPreparationSnapshot.h"
#include "processes/ImageIntegration/workflow/RegistrationWorkAnalyzer.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutor.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutionController.h"
#include "processes/ImageIntegration/workflow/WorkflowArchiver.h"
#include "processes/ImageIntegration/workflow/WorkflowPreparation.h"
#include "processes/ImageIntegration/workflow/WorkflowResultOpener.h"
#include "processes/ImageIntegration/workflow/WorkflowRunStateStore.h"
#include "processes/ImageIntegration/reporting/MarkdownHtmlRenderer.h"
#include "processes/ImageIntegration/reporting/WorkflowReportArchiveLoader.h"
#include "processes/ImageIntegration/persistence/AutosaveManager.h"
#include "processes/ImageIntegration/persistence/IntegrationDatabase.h"
#include "processes/ImageIntegration/persistence/RegistrationMetricsRepository.h"

#include "processes/ImageIntegration/persistence/PresetManager.h"
#include "processes/ImageIntegration/persistence/ProjectManager.h"
#include "processes/ImageIntegration/persistence/SessionDocument.h"
#include "processes/ImageIntegration/model/UndoCommands.h"
#include "processes/ImageIntegration/ImageIntegrationConstants.h"
#include "processes/ImageIntegration/ImageIntegrationImportSession.h"
#include "processes/ImageIntegration/ImageIntegrationProcessShared.h"
#include "processes/ImageIntegration/core/FrameDetection.h"
#include "processes/ImageIntegration/core/BlinkComparatorExclusionUtils.h"
#include "processes/ImageIntegration/core/FileImport.h"
#include "processes/ImageIntegration/core/DatabaseUtils.h"
#include "processes/ImageIntegration/core/StackingParamsUtils.h"
#include "controls/JsonSerializer.h"
#include "controls/PersistenceDbExecutor.h"
#include "theme/AnDesignTokens.h"

#include <QAction>
#include <QAbstractButton>
#include <QCoreApplication>
#include <QCheckBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QUrl>
#include <QSignalBlocker>
#include <QSettings>
#include <QTabWidget>
#include <QTabBar>
#include <QTimer>
#include <QMenu>
#include <QMenuBar>
#include <QShortcut>
#include <QUndoStack>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QAbstractItemView>
#include <QRegularExpression>
#include <QSaveFile>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFutureWatcher>
#include <QHash>
#include <QHeaderView>
#include <QProgressBar>
#include <QPromise>
#include <QPushButton>
#include <QPointer>
#include <QElapsedTimer>
#include <QPalette>
#include <QSizePolicy>
#include <QTableWidget>
#include <QStyle>
#include <QVBoxLayout>

#include <QtConcurrent/QtConcurrentRun>

#include <deque>
#include <QSet>

#include "processes/ImageIntegration/core/ImportProgressReporter.h"
#include "processes/ImageIntegration/dialogs/ImportProgressDialog.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <thread>
#include <unordered_set>
#include <vector>

#include <AfterNightPlatform/Logging.h>

using an::ui::CalibrationPreviewConfig;

namespace
{
enum class LogicalTabIndex : int
{
    Bias = 0,
    Dark,
    Flat,
    Light,
    Calibration,
    Registration,
    Stacking,
    Summary
};

enum class RootTabIndex : int
{
    FrameSources = 0,
    Workflow = 1
};

constexpr int toIndex(const LogicalTabIndex tab)
{
    return static_cast<int>(tab);
}

constexpr int toIndex(const RootTabIndex tab)
{
    return static_cast<int>(tab);
}

// Helpers that moved into ImageIntegrationProcessShared.cpp and ImageIntegrationProcessImport.cpp
// when the import session and the preset helpers left this unit. Named one by one so the call sites
// below read exactly as they did when these were file-local.
using ImageIntegration::detail::applyImportBatch;
using ImageIntegration::detail::checkOutputDirectoryBeforeRun;
using ImageIntegration::detail::frameTypeLabel;
using ImageIntegration::detail::importSummaryText;
using ImageIntegration::detail::integrationIcon;
using ImageIntegration::detail::integrationIconOrFallback;
using ImageIntegration::detail::notifyErrorOrDialog;
using ImageIntegration::detail::notifyInfoOrDialog;
using ImageIntegration::detail::notifyWarnOrDialog;
using ImageIntegration::detail::openNonBlockingMessageBox;
using ImageIntegration::detail::openNonBlockingQuestionBox;
using ImageIntegration::detail::openNonBlockingTextInput;
using ImageIntegration::detail::PreparedImportBatch;
using ImageIntegration::detail::prepareImportBatch;
using ImageIntegration::detail::refreshImportProgressDialog;
using ImageIntegration::detail::remapFrameInfoType;
using ImageIntegration::detail::remapMetadataType;
using ImageIntegration::detail::rememberDirectoryForPath;
using ImageIntegration::detail::rememberDirectoryPath;
using ImageIntegration::detail::rememberedExistingPath;

std::vector<FrameInfo> collectFramesForUndo(const IntegrationDataModel* model, FrameType type, const QStringList& paths)
{
    std::vector<FrameInfo> out;
    if (!model)
    {
        return out;
    }

    out.reserve(static_cast<size_t>(paths.size()));
    for (const auto& path : paths)
    {
        if (auto frame = model->frames(type).findById(FrameId(path)); frame.has_value())
        {
            out.push_back(*frame);
        }
    }
    return out;
}

QString formatBytesHuman(qint64 bytes)
{
    if (bytes <= 0)
    {
        return QStringLiteral("-");
    }

    constexpr qint64 kKB = 1024;
    constexpr qint64 kMB = 1024 * kKB;
    constexpr qint64 kGB = 1024 * kMB;

    const QLocale locale;
    if (bytes >= kGB)
    {
        return locale.toString(bytes / static_cast<double>(kGB), 'f', 2) + QStringLiteral(" GB");
    }
    if (bytes >= kMB)
    {
        return locale.toString(bytes / static_cast<double>(kMB), 'f', 1) + QStringLiteral(" MB");
    }
    if (bytes >= kKB)
    {
        return locale.toString(bytes / static_cast<double>(kKB), 'f', 1) + QStringLiteral(" KB");
    }
    return locale.toString(bytes) + QStringLiteral(" B");
}

QString summarizeValues(const QStringList& values)
{
    if (values.isEmpty())
    {
        return QStringLiteral("-");
    }
    const QString first = values.front();
    for (const auto& v : values)
    {
        if (v != first)
        {
            return QStringLiteral("Varies");
        }
    }
    return first;
}

QString boolLabel(bool value, const QString& on_label, const QString& off_label)
{
    return value ? on_label : off_label;
}

QString normalizationModeLabel(int mode)
{
    switch (static_cast<NormalizationMode>(mode))
    {
    case NormalizationMode::None:
        return QStringLiteral("None");
    case NormalizationMode::Global:
        return QStringLiteral("Global");
    case NormalizationMode::Local:
        return QStringLiteral("Local");
    }
    return QStringLiteral("Unknown");
}

QString globalNormalizationMethodLabel(int mode)
{
    switch (static_cast<GlobalNormalizationMethod>(mode))
    {
    case GlobalNormalizationMethod::Additive:
        return QStringLiteral("Additive");
    case GlobalNormalizationMethod::Multiplicative:
        return QStringLiteral("Multiplicative");
    case GlobalNormalizationMethod::AdditiveScaling:
        return QStringLiteral("Additive With Scaling");
    case GlobalNormalizationMethod::MultiplicativeScaling:
        return QStringLiteral("Multiplicative With Scaling");
    }
    return QStringLiteral("Additive With Scaling");
}

QString scaleEstimatorLabel(int method)
{
    switch (static_cast<ScaleEstimatorMethod>(method))
    {
    case ScaleEstimatorMethod::MAD:
        return QStringLiteral("MAD");
    case ScaleEstimatorMethod::IKSS:
        return QStringLiteral("IKSS");
    }
    return QStringLiteral("MAD");
}

QString formatFloatCompact(double value, int decimals = 2)
{
    QString s = QString::number(value, 'f', decimals);
    while (s.contains('.') && (s.endsWith('0') || s.endsWith('.')))
    {
        s.chop(1);
    }
    return s;
}

bool isAverageStackMethod(const QString& method)
{
    const QString normalized = method.trimmed().toLower();
    return normalized.contains(QStringLiteral("average")) || normalized == QStringLiteral("mean");
}

bool isWeightingEnabled(const QString& weighting)
{
    const QString normalized = weighting.trimmed().toLower();
    return !normalized.isEmpty() && normalized != QStringLiteral("none") && normalized != QStringLiteral("off") &&
           normalized != QStringLiteral("disabled");
}

QJsonObject findFirstObjectByKey(const QJsonArray& array, const QString& keyName, const QString& keyValue)
{
    for (const auto& v : array)
    {
        const QJsonObject obj = v.toObject();
        if (obj.value(keyName).toString() == keyValue)
        {
            return obj;
        }
    }
    return QJsonObject();
}

int tabIndexForFixTarget(FixTarget target)
{
    switch (target)
    {
    case FixTarget::BiasFrames:
        return toIndex(LogicalTabIndex::Bias);
    case FixTarget::DarkFrames:
        return toIndex(LogicalTabIndex::Dark);
    case FixTarget::FlatFrames:
        return toIndex(LogicalTabIndex::Flat);
    case FixTarget::LightFrames:
        return toIndex(LogicalTabIndex::Light);
    case FixTarget::CalibrationTab:
        return toIndex(LogicalTabIndex::Calibration);
    case FixTarget::RegistrationTab:
        return toIndex(LogicalTabIndex::Registration);
    case FixTarget::IntegrationTab:
        return toIndex(LogicalTabIndex::Stacking);
    case FixTarget::OutputDirectory:
        return toIndex(LogicalTabIndex::Summary); // Summary is the primary dashboard for fixes.
    case FixTarget::None:
    default:
        return toIndex(LogicalTabIndex::Summary);
    }
}

} // namespace

ImageIntegrationProcess::ImageIntegrationProcess(QObject* parent) : TopLevelProcessBaseControl(getProcessNameStatic())
{
    Q_UNUSED(parent);
    setProcessDescription(
        QStringLiteral("Run end-to-end calibration, registration, and stacking workflows for multi-frame datasets."));

    m_ui = an::ui::processes::buildImageIntegrationProcessUi(this);

    verticalLayout = m_ui.verticalLayout;
    mainMenuBar = m_ui.mainMenuBar;
    rootTabWidget = m_ui.rootTabWidget;
    sourceTabWidget = m_ui.sourceTabWidget;
    biasTab = m_ui.biasTab;
    darkTab = m_ui.darkTab;
    flatTab = m_ui.flatTab;
    lightTab = m_ui.lightTab;
    verticalLayout_2 = m_ui.verticalLayout_2;
    verticalLayout_3 = m_ui.verticalLayout_3;
    verticalLayout_4 = m_ui.verticalLayout_4;
    verticalLayout_5 = m_ui.verticalLayout_5;
    workflowTabWidget = m_ui.workflowTabWidget;
    calibrationTab = m_ui.calibrationTab;
    registrationTab = m_ui.registrationTab;
    stackingTab = m_ui.stackingTab;
    summaryTab = m_ui.summaryTab;
    outputDirectoryEdit = m_ui.outputDirectoryEdit;
    outputDirectoryBrowseButton = m_ui.outputDirectoryBrowseButton;
    dialogButtonBox = m_ui.dialogButtonBox;
    if (outputDirectoryBrowseButton)
    {
        outputDirectoryBrowseButton->setProperty("anButtonVariant", QStringLiteral("secondary"));
    }

    // Ensure background is painted during interactive resizing; without this some styles can leave
    // stale pixels when large child widgets move/resize within tabs (appearing as overlap trails).
    setAutoFillBackground(true);
    setAttribute(Qt::WA_StyledBackground, true);
    if (verticalLayout)
    {
        // Prevent the top-level window's minimum size from "jumping" as different tabs
        // compute different minimum layout sizes (Qt's default constraint for top-level
        // widgets is to apply the layout minimum size to the window).
        verticalLayout->setSizeConstraint(QLayout::SetNoConstraint);
    }
    setWindowTitle(tr("Image Integration"));

    m_model = std::make_unique<IntegrationDataModel>();
    m_sessionName = QStringLiteral("last_session");

    const QString data_dir = an::ui::applicationLocalDataPath();
    QDir dir(data_dir);
    if (!dir.exists())
    {
        (void)dir.mkpath(QStringLiteral("."));
    }

    m_database = std::make_unique<IntegrationDatabase>(dir.filePath(QStringLiteral("image_integration_sessions.db")));
    // What the person is told about the stored sessions once the window is up. A database of another format is never
    // read, written or deleted: it is set aside beside itself and a new one made in its place. A database or a session
    // that cannot be read otherwise is left as it is, and nothing is saved over it automatically.
    QMessageBox::Icon startupIcon = QMessageBox::Information;
    QString startupMessage;
    bool opened = m_database->initialize();
    if (!opened && m_database->otherFormat().has_value())
    {
        const QString refusal = m_database->lastError();
        const QString setAside = m_database->setAside();
        if (!setAside.isEmpty())
        {
            startupMessage = tr("%1 The saved sessions were set aside as %2, and new sessions are saved from now on.")
                                 .arg(refusal, QDir::toNativeSeparators(setAside));
            opened = m_database->initialize();
        }
    }
    if (opened)
    {
        const SessionLoadResult loaded = m_database->loadSessionResult(m_sessionName);
        if (loaded.model.has_value())
        {
            m_model->fromJson(*loaded.model);
        }

        if (!loaded.error.isEmpty())
        {
            LOG_WARNING("ImageIntegrationProcess: the stored session '{}' was refused and kept as it is: {}",
                        m_sessionName.toStdString(), loaded.error.toStdString());
            startupIcon = QMessageBox::Warning;
            startupMessage = tr("The last Image Integration session could not be restored and was kept as it is. "
                                "Changes in this window are not saved automatically.\n\n%1")
                                 .arg(loaded.error);
        }
        else
        {
            m_autosave = std::make_unique<AutosaveManager>(m_model.get(), m_database.get(), m_sessionName);
            connect(m_autosave.get(), &AutosaveManager::autosaveFailed, this, [this](const QString& msg) {
                const QString title = tr("Autosave");
                const QString summary = tr("Autosave failed: %1").arg(msg);
                notifyWarnOrDialog(this, title, summary);
            });
            m_autosave->start(ImageIntegration::Config::kAutosaveIntervalMs);
        }
    }
    else
    {
        const QString reason = m_database->lastError();
        LOG_WARNING("ImageIntegrationProcess: the session database was refused and left as it is: {}",
                    reason.toStdString());
        startupIcon = QMessageBox::Warning;
        startupMessage = tr("The saved Image Integration sessions could not be opened and were left as they are. "
                            "Changes in this window are not saved automatically.\n\n%1")
                             .arg(reason.isEmpty() ? tr("The session database could not be opened.") : reason);
        m_database.reset();
    }

    if (!startupMessage.isEmpty())
    {
        QTimer::singleShot(0, this, [this, startupIcon, startupMessage]() {
            openNonBlockingMessageBox(this, startupIcon, tr("Saved Sessions"), startupMessage);
        });
    }

    m_presetManager = std::make_unique<PresetManager>(dir.filePath(QStringLiteral("presets")));
    m_projectManager = std::make_unique<ProjectManager>();
    {
        QSettings settings = an::ui::applicationSettings();
        m_projectManager->loadFromSettings(settings);
    }
    m_undoStack = new QUndoStack(this);
    m_undoStack->setUndoLimit(100); // Limit to prevent unbounded memory growth

    initialize();
}

std::string ImageIntegrationProcess::getProcessNameStatic()
{
    return "Image Integration";
}

void ImageIntegrationProcess::refreshTheme()
{
    const auto iconOrFallback = [this](const QString& iconName, const QStyle::StandardPixmap fallback) -> QIcon {
        return integrationIconOrFallback(this, iconName, fallback);
    };

    const auto applyIconToAction = [&iconOrFallback](QWidget* root, const char* objectName, const QString& iconName,
                                                     const QStyle::StandardPixmap fallback) {
        if (!root || !objectName)
        {
            return;
        }
        if (auto* action = root->findChild<QAction*>(QString::fromUtf8(objectName)))
        {
            action->setIcon(iconOrFallback(iconName, fallback));
        }
    };

    const auto applyIconToButton = [&iconOrFallback](QWidget* root, const char* objectName, const QString& iconName,
                                                     const QStyle::StandardPixmap fallback) {
        if (!root || !objectName)
        {
            return;
        }
        if (auto* button = root->findChild<QAbstractButton*>(QString::fromUtf8(objectName)))
        {
            button->setIcon(iconOrFallback(iconName, fallback));
        }
    };

    TopLevelProcessBaseControl::refreshTheme();

    setupTabIcons();

    if (m_addFilesAction)
    {
        m_addFilesAction->setIcon(iconOrFallback(QStringLiteral("file_open"), QStyle::SP_DialogOpenButton));
    }
    if (m_addDirectoryAction)
    {
        m_addDirectoryAction->setIcon(iconOrFallback(QStringLiteral("folder_open"), QStyle::SP_DirOpenIcon));
    }
    if (m_clearSelectedAction)
    {
        m_clearSelectedAction->setIcon(iconOrFallback(QStringLiteral("delete"), QStyle::SP_TrashIcon));
    }
    if (m_clearAllAction)
    {
        m_clearAllAction->setIcon(iconOrFallback(QStringLiteral("close"), QStyle::SP_DialogCloseButton));
    }
    if (m_importProjectAction)
    {
        m_importProjectAction->setIcon(iconOrFallback(QStringLiteral("file_open"), QStyle::SP_DialogOpenButton));
    }
    if (m_exportProjectAction)
    {
        m_exportProjectAction->setIcon(iconOrFallback(QStringLiteral("file_save"), QStyle::SP_DialogSaveButton));
    }
    if (m_resetAction)
    {
        m_resetAction->setIcon(iconOrFallback(QStringLiteral("refresh"), QStyle::SP_BrowserReload));
    }
    if (m_executeAction)
    {
        m_executeAction->setIcon(iconOrFallback(QStringLiteral("play"), QStyle::SP_MediaPlay));
    }
    if (m_recentProjectsMenu)
    {
        m_recentProjectsMenu->menuAction()->setIcon(
            iconOrFallback(QStringLiteral("history"), QStyle::SP_FileDialogDetailedView));
    }

    if (outputDirectoryBrowseButton)
    {
        outputDirectoryBrowseButton->setIcon(iconOrFallback(QStringLiteral("folder_open"), QStyle::SP_DirOpenIcon));
    }

    for (auto* tab : m_frameSourceTabs)
    {
        if (!tab)
        {
            continue;
        }
        applyIconToButton(tab, "actionAddFiles", QStringLiteral("file_open"), QStyle::SP_DialogOpenButton);
        applyIconToButton(tab, "actionAddFolder", QStringLiteral("folder_open"), QStyle::SP_DirOpenIcon);
        applyIconToButton(tab, "actionRemoveSelected", QStringLiteral("delete"), QStyle::SP_TrashIcon);
        applyIconToButton(tab, "actionRemoveAll", QStringLiteral("close"), QStyle::SP_DialogCloseButton);
        applyIconToAction(tab, "actionAddFolderDefault", QStringLiteral("folder_open"), QStyle::SP_DirOpenIcon);

        // Refresh labels that derive semantic colours from the active palette/tokens.
        tab->refreshQuickStats();
        tab->refreshFilterStats();
    }

    applyIconToButton(m_registrationTab, "btnRunRegistration", QStringLiteral("play"), QStyle::SP_MediaPlay);
    applyIconToButton(m_registrationTab, "btnRunRegistrationGroup", QStringLiteral("play"), QStyle::SP_MediaPlay);
    applyIconToButton(m_registrationTab, "btnClearRegistrationGroup", QStringLiteral("delete"), QStyle::SP_TrashIcon);
    applyIconToButton(m_stackingTab, "btnRunIntegration", QStringLiteral("play"), QStyle::SP_MediaPlay);
    applyIconToButton(m_stackingTab, "btnRunIntegrationGroup", QStringLiteral("play"), QStyle::SP_MediaPlay);

    // Refresh theme for all frame tree views
    for (auto* tree : m_frameTrees)
    {
        if (tree)
        {
            tree->viewport()->update();
        }
    }

    // Refresh preview panels
    for (auto* tab : m_frameSourceTabs)
    {
        if (tab && tab->previewPanel())
        {
            tab->previewPanel()->update();
        }
    }

    // Refresh configuration tabs
    if (m_calibrationTab)
    {
        m_calibrationTab->update();
    }
    if (m_registrationTab)
    {
        m_registrationTab->update();
    }
    if (m_stackingTab)
    {
        m_stackingTab->update();
    }
    if (m_summaryTab)
    {
        m_summaryTab->update();
    }

    // Ensure visible tab badges pick up refreshed token colors/fonts.
    updateTabBadgesNow();
}

ImageIntegrationProcess::~ImageIntegrationProcess()
{
    QCoreApplication::removePostedEvents(this, QEvent::MetaCall);

    if (m_tabBadgeDebounce)
    {
        m_tabBadgeDebounce->stop();
    }

    // Model-bound objects own raw model pointers internally, so they must be torn down
    // before the model itself. The model is intentionally not parented to this widget.
    m_modelBoundCleanup.clear();
    m_autosave.reset();
    m_contextMenuHandler = nullptr;
    m_dragDropHandler = nullptr;
    m_calibrationTab = nullptr;
    m_registrationTab = nullptr;
    m_stackingTab = nullptr;
    m_summaryTab = nullptr;
    m_frameSourceTabs.fill(nullptr);
    m_frameTrees.fill(nullptr);
    m_model.reset();
}

IntegrationDataModel* ImageIntegrationProcess::model() const noexcept
{
    return m_model.get();
}

void ImageIntegrationProcess::setProjectName(const QString& name)
{
    if (m_projectManager)
    {
        m_projectManager->setProjectName(name);
    }
    updateWindowTitle();
}

QString ImageIntegrationProcess::projectName() const
{
    return m_projectManager ? m_projectManager->projectName() : QString();
}

QString ImageIntegrationProcess::currentProjectPath() const
{
    return m_projectManager ? m_projectManager->currentProjectPath() : QString();
}

QStringList ImageIntegrationProcess::recentProjects() const
{
    return m_projectManager ? m_projectManager->recentProjects() : QStringList{};
}

void ImageIntegrationProcess::updateWindowTitle()
{
    QString title = tr("Image Integration");
    const QString name = projectName();
    if (!name.isEmpty())
    {
        title += QStringLiteral(" - %1").arg(name);
    }
    if (m_model && m_model->hasUnsavedChanges())
    {
        title += QLatin1Char('*');
    }

    setWindowTitle(title);
}

void ImageIntegrationProcess::closeEvent(QCloseEvent* evt)
{
    if (m_autosave)
    {
        m_autosave->triggerNow();
        m_autosave->stop();
    }

    TopLevelProcessBaseControl::closeEvent(evt);
}

std::string ImageIntegrationProcess::serialize_json_settings()
{
    // Packet 7C: nlohmann JSON is the standard for process settings serialization.
    an::ui::json_serializer::json doc;
    doc["version"] = 1;
    return an::ui::json_serializer::dump_compact(doc);
}

void ImageIntegrationProcess::deserialize_json_settings(const std::string& json_settings)
{
    // Packet 7C: nlohmann JSON deserialization with safe_parse.
    const auto doc = an::ui::json_serializer::safe_parse(json_settings);
    if (!doc)
        return;

    // Future: restore process-specific settings from (*doc)["params"].
}

void ImageIntegrationProcess::initialize()
{
    setupMenuBar();
    setupTabs();
    setupConnections();
    setupShortcuts();
    applySplitterDefaults();

    initializeTopLevelProcess();
    onOutputDirectoryChanged(m_model->outputDirectory());
    autoDetectMasters();
    updateButtonStates();
    updateWindowTitle();
}

void ImageIntegrationProcess::setupShortcuts()
{
    auto* refresh = new QShortcut(QKeySequence(Qt::Key_F5), this);
    connect(refresh, &QShortcut::activated, this, [this]() {
        refreshTheme();
        if (m_registrationTab)
        {
            m_registrationTab->refreshAll();
        }
        if (m_stackingTab)
        {
            m_stackingTab->refreshAll();
        }
        scheduleTabBadgeRefresh();
        updateFrameTabLabels();
    });
}

void ImageIntegrationProcess::setupTabs()
{
    // Frame source tabs (tree + context panel per frame type)
    std::array<QVBoxLayout*, kFrameTabCount> layouts = {verticalLayout_2, verticalLayout_3, verticalLayout_4,
                                                        verticalLayout_5};

    for (int i = 0; i < kFrameTabCount; ++i)
    {
        const FrameType type = frameTypeForIndex(i);
        QWidget* page_parent = layouts[i] ? layouts[i]->parentWidget() : this;
        m_frameSourceTabs[i] = new FrameSourceTab(type, m_model.get(), page_parent, m_undoStack);
        m_modelBoundCleanup.add(m_frameSourceTabs[i]);

        connect(m_frameSourceTabs[i], &FrameSourceTab::addFilesRequested, this,
                [this](FrameType requestedType) { importFilesDialog(requestedType, /*forceToType=*/true); });
        connect(m_frameSourceTabs[i], &FrameSourceTab::addFolderRequested, this,
                [this](FrameType requestedType, bool recursive) {
                    importFolderDialog(requestedType, /*forceToType=*/true, recursive);
                });
        connect(m_frameSourceTabs[i], &FrameSourceTab::removeSelectedRequested, this, [this]() { onClearSelected(); });
        connect(m_frameSourceTabs[i], &FrameSourceTab::removeAllRequested, this,
                [this](FrameType type) { onClearAllInTab(type); });

        if (layouts[i])
        {
            layouts[i]->setContentsMargins(0, 0, 0, 0);
            layouts[i]->setSpacing(0);
            layouts[i]->addWidget(m_frameSourceTabs[i]);
        }

        m_frameTrees[i] = m_frameSourceTabs[i]->tree();
    }

    // Create context menu handler (shared across all trees)
    m_contextMenuHandler = new TreeContextMenuHandler(m_model.get(), this, m_undoStack);
    m_modelBoundCleanup.add(m_contextMenuHandler);

    // Create drag-drop handler
    m_dragDropHandler = new FrameTreeDragDropHandler(this);
    m_modelBoundCleanup.add(m_dragDropHandler);
    for (int i = 0; i < kFrameTabCount; ++i)
    {
        if (m_frameTrees[i])
        {
            m_dragDropHandler->registerDropTarget(m_frameTrees[i], frameTypeForIndex(i));
        }
    }

    // Config tabs - replacing placeholder widgets if necessary or just adding to layouts
    // In .ui, calibrationTab etc. are just QWidgets.
    auto addTabWidget = [this](QWidget* placeholder, QWidget* actual) {
        if (!placeholder || !actual)
            return;
        auto* layout = new QVBoxLayout(placeholder);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(actual);
    };

    m_calibrationTab = new CalibrationTab(m_model.get(), this, m_undoStack);
    m_modelBoundCleanup.add(m_calibrationTab);
    addTabWidget(calibrationTab, m_calibrationTab);
    QObject::connect(m_calibrationTab, &CalibrationTab::goToGroupDefinitionRequested, this,
                     &ImageIntegrationProcess::goToGroupDefinition);

    m_registrationTab = new RegistrationTab(m_model.get(), this, m_undoStack);
    m_modelBoundCleanup.add(m_registrationTab);
    addTabWidget(registrationTab, m_registrationTab);

    m_stackingTab = new StackingTab(m_model.get(), this, m_undoStack);
    m_modelBoundCleanup.add(m_stackingTab);
    addTabWidget(stackingTab, m_stackingTab);

    m_summaryTab = new SummaryTab(m_model.get(), this);
    m_modelBoundCleanup.add(m_summaryTab);
    addTabWidget(summaryTab, m_summaryTab);

    // Add tab icons for visual differentiation.
    setupTabIcons();

    // Add notification badges to tabs indicating issues.
    const auto createTabIssueBadge = [this](QTabBar* bar, const QString& objectName) -> QLabel* {
        if (!bar)
        {
            return nullptr;
        }

        auto* badge = new TabIssueBadge(bar);
        badge->setObjectName(objectName);
        badge->setVisible(false);
        badge->setAlignment(Qt::AlignCenter);
        badge->setProperty("anTabIssueBadge", true);
        applyBadgeSeverityStyle(badge, ValidationIssue::Severity::Warning);
        return badge;
    };

    if (sourceTabWidget)
    {
        auto* bar = sourceTabWidget->tabBar();
        for (int i = 0; i < kFrameTabCount; ++i)
        {
            auto* badge = createTabIssueBadge(bar, QStringLiteral("childBadge_%1").arg(i));
            if (!badge)
            {
                continue;
            }
            bar->setTabButton(i, QTabBar::RightSide, badge);
            m_tabBadges[static_cast<size_t>(i)] = badge;
        }
    }

    if (workflowTabWidget)
    {
        auto* bar = workflowTabWidget->tabBar();
        for (int i = 0; i < kFrameTabCount; ++i)
        {
            const int logicalIndex = i + toIndex(LogicalTabIndex::Calibration);
            auto* badge = createTabIssueBadge(bar, QStringLiteral("childBadge_%1").arg(logicalIndex));
            if (!badge)
            {
                continue;
            }
            bar->setTabButton(i, QTabBar::RightSide, badge);
            m_tabBadges[static_cast<size_t>(logicalIndex)] = badge;
        }
    }

    if (rootTabWidget)
    {
        auto* bar = rootTabWidget->tabBar();
        for (int i = 0; i < kParentTabCount; ++i)
        {
            auto* badge = createTabIssueBadge(bar, QStringLiteral("parentBadge_%1").arg(i));
            if (!badge)
            {
                continue;
            }
            bar->setTabButton(i, QTabBar::RightSide, badge);
            m_parentTabBadges[static_cast<size_t>(i)] = badge;
        }
    }

    // Update badges synchronously on first run to prevent flicker.
    // Subsequent updates can use the debounced version.
    updateTabBadgesNow();
}

void ImageIntegrationProcess::scheduleTabBadgeRefresh()
{
    if (!m_tabBadgeDebounce)
    {
        m_tabBadgeDebounce = new QTimer(this);
        m_tabBadgeDebounce->setSingleShot(true);
        m_tabBadgeDebounce->setInterval(200);
        connect(m_tabBadgeDebounce, &QTimer::timeout, this, &ImageIntegrationProcess::updateTabBadgesNow);
    }

    m_tabBadgeDebounce->start();
}

void ImageIntegrationProcess::updateTabBadgesNow()
{
    if (!rootTabWidget || !m_model)
    {
        return;
    }

    const auto forceTabRelayout = [](QTabWidget* tabs) {
        if (!tabs)
        {
            return;
        }

        // Qt doesn't always recompute tab widths when a tab button widget (badge)
        // changes visibility/text. Nudge it by re-applying tab text.
        for (int i = 0; i < tabs->count(); ++i)
        {
            tabs->setTabText(i, tabs->tabText(i));
        }

        if (auto* bar = tabs->tabBar())
        {
            bar->updateGeometry();
            bar->update();
        }
        tabs->updateGeometry();
        tabs->update();
    };

    std::array<int, kLogicalTabCount> counts{};
    std::array<std::optional<ValidationIssue::Severity>, kLogicalTabCount> severity{};
    std::array<std::vector<const ValidationIssue*>, kLogicalTabCount> issues_by_tab;

    const WorkflowValidationResult validation = m_model->validate();

    const auto buildTooltipForIssues = [this](const std::vector<const ValidationIssue*>& issues) -> QString {
        if (issues.empty())
        {
            return QString();
        }

        int error_count = 0;
        int warning_count = 0;
        for (const auto* issue : issues)
        {
            if (!issue)
            {
                continue;
            }
            if (issue->severity == ValidationIssue::Severity::Error)
            {
                ++error_count;
            }
            else
            {
                ++warning_count;
            }
        }

        QStringList lines;
        if (error_count > 0 && warning_count > 0)
        {
            lines
                << tr("%1 issue(s): %2 error(s), %3 warning(s)").arg(issues.size()).arg(error_count).arg(warning_count);
        }
        else
        {
            lines << tr("%1 issue(s) in this tab").arg(issues.size());
        }

        // De-duplicate and keep the tooltip bounded.
        constexpr int kMaxLines = 20;
        QSet<QString> seen;

        // Prefer errors first, then warnings.
        auto append_issue = [&lines, &seen](const ValidationIssue& issue) {
            QString line = QStringLiteral("%1 %2").arg(issue.severityIcon(), issue.message);
            if (!issue.filePath.isEmpty())
            {
                line += QStringLiteral(" (%1)").arg(issue.filePath);
            }
            if (!seen.contains(line))
            {
                seen.insert(line);
                lines << line;
            }
        };

        for (const auto* issue : issues)
        {
            if (!issue || issue->severity != ValidationIssue::Severity::Error)
            {
                continue;
            }
            append_issue(*issue);
            if (lines.size() >= (kMaxLines + 1))
            {
                break;
            }
        }
        for (const auto* issue : issues)
        {
            if (!issue || issue->severity != ValidationIssue::Severity::Warning)
            {
                continue;
            }
            append_issue(*issue);
            if (lines.size() >= (kMaxLines + 1))
            {
                break;
            }
        }

        const int listed = lines.size() - 1; // exclude header line

        // If we hit the cap, provide a hint that more issues exist.
        // (We intentionally cap tooltip size to avoid huge UI stalls.)
        if (listed >= kMaxLines)
        {
            const int remaining = static_cast<int>(issues.size()) - listed;
            if (remaining > 0)
            {
                lines << tr("…and %1 more").arg(remaining);
            }
        }

        return lines.join(QStringLiteral("\n"));
    };

    struct CalibrationBadgeDetails
    {
        int rawIssueCount = 0;
        int displayedCount = 0;
        int biasGroups = 0;
        int darkGroups = 0;
        int flatGroups = 0;
        QString toolTip;
        QString badgeText;
        std::optional<ValidationIssue::Severity> worstSeverity;
    };
    std::optional<CalibrationBadgeDetails> calibrationDetails;

    for (const auto& issue : validation.issues)
    {
        const int tab_index = tabIndexForFixTarget(issue.fixTarget);
        if (tab_index < 0 || tab_index >= kLogicalTabCount)
        {
            continue;
        }

        counts[static_cast<size_t>(tab_index)] += 1;
        issues_by_tab[static_cast<size_t>(tab_index)].push_back(&issue);
        auto& sev = severity[static_cast<size_t>(tab_index)];
        if (!sev.has_value() || issue.severity == ValidationIssue::Severity::Error)
        {
            sev = issue.severity;
        }
    }

    // Special case: Calibration tab badge should explain what's being counted.
    // The calibration UI is group-centric, showing: Missing (red), Fallback (yellow), OK (green).
    // Badge should only count truly missing (red cells), with fallback as informational.
    {
        constexpr int kCalibrationTabIndex = toIndex(LogicalTabIndex::Calibration);

        // Track missing groups (red cells) separately from fallback groups (yellow cells).
        QSet<QString> missing_bias_groups;
        QSet<QString> missing_dark_groups;
        QSet<QString> missing_flat_groups;
        QSet<QString> missing_dark_or_bias_groups;
        QSet<QString> fallback_dark_groups;
        QSet<QString> fallback_flat_groups;

        int missing_count = 0;
        int fallback_count = 0;
        std::optional<ValidationIssue::Severity> worst;

        for (const auto& issue : validation.issues)
        {
            if (tabIndexForFixTarget(issue.fixTarget) != kCalibrationTabIndex)
            {
                continue;
            }

            if (!worst.has_value() || issue.severity == ValidationIssue::Severity::Error)
            {
                worst = issue.severity;
            }

            // The validation now passes group labels (not file paths) in issue.filePath.
            const QString group_label = issue.filePath.isEmpty() ? tr("Unknown") : issue.filePath;

            if (issue.code == ValidationErrorCode::MissingCalibrationMatch)
            {
                ++missing_count;
                if (issue.missingCalibrationType.has_value())
                {
                    switch (issue.missingCalibrationType.value())
                    {
                    case CalibrationFrameType::Bias:
                        missing_bias_groups.insert(group_label);
                        break;
                    case CalibrationFrameType::Dark:
                        missing_dark_groups.insert(group_label);
                        break;
                    case CalibrationFrameType::Flat:
                        missing_flat_groups.insert(group_label);
                        break;
                    case CalibrationFrameType::DarkOrBias:
                        missing_dark_or_bias_groups.insert(group_label);
                        break;
                    }
                }
            }
            else if (issue.code == ValidationErrorCode::FallbackCalibrationMatch)
            {
                ++fallback_count;
                if (issue.missingCalibrationType.has_value())
                {
                    switch (issue.missingCalibrationType.value())
                    {
                    case CalibrationFrameType::Bias:
                        // Bias doesn't have fallback in current matcher.
                        break;
                    case CalibrationFrameType::Dark:
                        fallback_dark_groups.insert(group_label);
                        break;
                    case CalibrationFrameType::Flat:
                        fallback_flat_groups.insert(group_label);
                        break;
                    case CalibrationFrameType::DarkOrBias:
                        // Not a valid fallback type.
                        break;
                    }
                }
            }
        }

        // Build badge details: badge number = missing groups count, tooltip explains everything.
        if (missing_count > 0 || fallback_count > 0)
        {
            CalibrationBadgeDetails details;
            details.rawIssueCount = missing_count + fallback_count;
            details.biasGroups = static_cast<int>(missing_bias_groups.size());
            details.darkGroups = static_cast<int>(missing_dark_groups.size());
            details.flatGroups = static_cast<int>(missing_flat_groups.size());
            details.worstSeverity = worst;

            // Badge text shows count of groups with truly MISSING calibration (red cells).
            QSet<QString> all_missing_groups = missing_bias_groups;
            all_missing_groups.unite(missing_dark_groups);
            all_missing_groups.unite(missing_flat_groups);
            all_missing_groups.unite(missing_dark_or_bias_groups);
            const int missing_groups = all_missing_groups.size();

            QSet<QString> all_fallback_groups = fallback_dark_groups;
            all_fallback_groups.unite(fallback_flat_groups);
            const int fallback_groups = all_fallback_groups.size();

            // If there are missing groups, show that count. Otherwise show fallback count.
            const int badge_number = (missing_groups > 0) ? missing_groups : fallback_groups;
            details.displayedCount = badge_number;
            details.badgeText = (badge_number > 99) ? QStringLiteral("99+") : QString::number(badge_number);

            QStringList lines;

            // Full summary.
            int total_light_groups = 0;
            for (const auto& g : m_model->lightGroups())
            {
                if (!g.frames.empty())
                {
                    ++total_light_groups;
                }
            }

            QSet<QString> not_ok_groups = all_missing_groups;
            not_ok_groups.unite(all_fallback_groups);
            int ok_groups = total_light_groups - not_ok_groups.size();
            if (ok_groups < 0)
            {
                ok_groups = 0;
            }

            if (ok_groups > 0)
            {
                lines << tr("✓ %1 light group(s) fully calibrated").arg(ok_groups);
            }

            // Lead with what the badge number represents.
            if (missing_groups > 0)
            {
                // lines << QString(); // Optional spacer if we want spacing after checkmark
                lines << tr("✗ %1 light group(s) missing calibration").arg(missing_groups);

                // Show which calibration types are missing.
                QStringList missing_types;
                if (!missing_bias_groups.isEmpty())
                {
                    missing_types << tr("Bias (%1)").arg(missing_bias_groups.size());
                }
                if (!missing_dark_groups.isEmpty())
                {
                    missing_types << tr("Dark (%1)").arg(missing_dark_groups.size());
                }
                if (!missing_flat_groups.isEmpty())
                {
                    missing_types << tr("Flat (%1)").arg(missing_flat_groups.size());
                }
                if (!missing_dark_or_bias_groups.isEmpty())
                {
                    missing_types << tr("Dark/Bias (%1)").arg(missing_dark_or_bias_groups.size());
                }
                if (!missing_types.isEmpty())
                {
                    lines << tr("   Missing: %1").arg(missing_types.join(QStringLiteral(", ")));
                }
            }

            // Show fallback info (yellow cells) if any.
            if (fallback_groups > 0)
            {
                // if (missing_groups > 0) lines << QString();
                lines << tr("⚠ %1 group(s) using fallback calibration").arg(fallback_groups);
                QStringList fallback_types;
                if (!fallback_dark_groups.isEmpty())
                {
                    fallback_types << tr("Dark (%1)").arg(fallback_dark_groups.size());
                }
                if (!fallback_flat_groups.isEmpty())
                {
                    fallback_types << tr("Flat (%1)").arg(fallback_flat_groups.size());
                }
                if (!fallback_types.isEmpty())
                {
                    lines << tr("   Fallback: %1").arg(fallback_types.join(QStringLiteral(", ")));
                }
                // lines << tr("(Using nearby session - yellow cells in table)");
            }

            lines << QString();
            lines << tr("Click to open Calibration tab.");
            details.toolTip = lines.join(QStringLiteral("\n"));

            calibrationDetails = details;
        }
    }

    bool any_badge_geometry_changed = false;
    for (int i = 0; i < kLogicalTabCount; ++i)
    {
        QLabel* badge = m_tabBadges[static_cast<size_t>(i)];
        if (!badge)
        {
            continue;
        }

        const bool wasVisible = badge->isVisible();
        const QString oldText = badge->text();
        const QColor oldBadgeColor = badge->palette().color(QPalette::Window);

        int count = counts[static_cast<size_t>(i)]; // Use mutable local var for adjustment

        if (i == toIndex(LogicalTabIndex::Calibration) && calibrationDetails.has_value())
        {
            // Sync the count to what is displayed on the badge
            count = calibrationDetails->displayedCount;
            counts[static_cast<size_t>(i)] = count; // Update array for parent aggregation
        }

        if (count <= 0)
        {
            badge->setVisible(false);
            badge->setToolTip(QString());

            if (wasVisible)
            {
                any_badge_geometry_changed = true;
            }
            continue;
        }

        QString text = (count > 99) ? QStringLiteral("99+") : QString::number(count);
        QString tooltip = buildTooltipForIssues(issues_by_tab[static_cast<size_t>(i)]);

        auto sev = severity[static_cast<size_t>(i)].value_or(ValidationIssue::Severity::Warning);
        if (i == toIndex(LogicalTabIndex::Calibration) && calibrationDetails.has_value())
        {
            text = calibrationDetails->badgeText;
            tooltip = calibrationDetails->toolTip;
            sev = calibrationDetails->worstSeverity.value_or(sev);
        }

        badge->setText(text);
        badge->setToolTip(tooltip);

        applyBadgeSeverityStyle(badge, sev);
        badge->setVisible(true);

        if (!wasVisible || oldText != badge->text() || oldBadgeColor != badge->palette().color(QPalette::Window))
        {
            any_badge_geometry_changed = true;
        }
    }

    // Propagate status to parent tabs
    if (rootTabWidget)
    {
        bool any_parent_badge_geometry_changed = false;
        for (int p = 0; p < kParentTabCount; ++p)
        {
            QLabel* parentBadge = m_parentTabBadges[static_cast<size_t>(p)];
            if (!parentBadge)
                continue;

            const bool wasVisible = parentBadge->isVisible();
            const QString oldText = parentBadge->text();

            int totalIssues = 0;
            auto worstSev = std::optional<ValidationIssue::Severity>();

            const int start = (p == toIndex(RootTabIndex::FrameSources)) ? 0 : toIndex(LogicalTabIndex::Calibration);
            const int end =
                (p == toIndex(RootTabIndex::FrameSources)) ? toIndex(LogicalTabIndex::Calibration) : kLogicalTabCount;

            for (int i = start; i < end; ++i)
            {
                if (counts[static_cast<size_t>(i)] > 0)
                {
                    totalIssues += counts[static_cast<size_t>(i)];
                    auto s = severity[static_cast<size_t>(i)];
                    if (!worstSev.has_value() || (s.has_value() && *s == ValidationIssue::Severity::Error))
                    {
                        worstSev = s;
                    }
                }
            }

            if (totalIssues > 0)
            {
                parentBadge->setVisible(true);
                QString text = (totalIssues > 99) ? QStringLiteral("99+") : QString::number(totalIssues);
                parentBadge->setText(text);

                auto s = worstSev.value_or(ValidationIssue::Severity::Warning);
                applyBadgeSeverityStyle(parentBadge, s);

                QStringList summaryLines;
                const int errCount =
                    (s == ValidationIssue::Severity::Error) ? 1 : 0; // Rough heuristic for header color/tone

                // Header
                summaryLines << (totalIssues == 1 ? tr("1 Issue Found:") : tr("%1 Issues Found:").arg(totalIssues));

                for (int i = start; i < end; ++i)
                {
                    const int c = counts[static_cast<size_t>(i)];
                    if (c > 0)
                    {
                        QString name;
                        if (p == 0 && sourceTabWidget)
                            name = sourceTabWidget->tabText(i);
                        else if (p == 1 && workflowTabWidget)
                            name = workflowTabWidget->tabText(i - 4);

                        // Clean name
                        int paren = name.lastIndexOf(" (");
                        if (paren > 0)
                            name = name.left(paren);

                        // Determine type label
                        auto tabSev = severity[static_cast<size_t>(i)].value_or(ValidationIssue::Severity::Warning);
                        QString typeLabel =
                            (tabSev == ValidationIssue::Severity::Error) ? tr("Errors") : tr("Warnings");

                        // Format: "• Calibration Warnings (5)"
                        summaryLines << tr("• %1 %2 (%3)").arg(name, typeLabel, QString::number(c));
                    }
                }
                parentBadge->setToolTip(summaryLines.join(QStringLiteral("\n")));

                if (!wasVisible || oldText != parentBadge->text())
                {
                    any_parent_badge_geometry_changed = true;
                }
            }
            else
            {
                parentBadge->setVisible(false);

                if (wasVisible)
                {
                    any_parent_badge_geometry_changed = true;
                }
            }
        }

        if (any_parent_badge_geometry_changed)
        {
            any_badge_geometry_changed = true;
        }
    }

    if (any_badge_geometry_changed)
    {
        forceTabRelayout(sourceTabWidget);
        forceTabRelayout(workflowTabWidget);
        forceTabRelayout(rootTabWidget);
    }
}

void ImageIntegrationProcess::updateFrameTabLabels()
{
    if (!sourceTabWidget || !m_model)
    {
        return;
    }

    // Frame type tab names with counts
    const std::array<QString, 4> baseNames = {tr("Bias"), tr("Darks"), tr("Flats"), tr("Lights")};

    const std::array<FrameType, kFrameTabCount> types = {FrameType::Bias, FrameType::Dark, FrameType::Flat,
                                                         FrameType::Light};

    for (int i = 0; i < kFrameTabCount; ++i)
    {
        const size_t count = m_model->frames(types[static_cast<size_t>(i)]).size();
        QString tabText = baseNames[static_cast<size_t>(i)];
        if (count > 0)
        {
            tabText += QStringLiteral(" (%1)").arg(count);
        }
        sourceTabWidget->setTabText(i, tabText);
    }
}

void ImageIntegrationProcess::updateLightCalibrationPreview(const QString& selectedPath)
{
    const int lightIndex = toIndex(LogicalTabIndex::Light);
    FrameSourceTab* tab = frameSourceTabForIndex(lightIndex);
    ImageViewControl* preview = tab ? tab->previewPanel() : nullptr;

    CalibrationPreviewConfig config;
    if (!preview)
    {
        return;
    }

    const auto disable = [&](const QString& reason) {
        config.available = false;
        config.tooltip = reason.trimmed();
        preview->setCalibrationPreviewConfig(config);
    };

    if (!m_model || selectedPath.isEmpty())
    {
        if (!m_model)
        {
            disable(tr("Calibration preview is unavailable (no active integration session)."));
        }
        else
        {
            disable(tr("Select a light frame to enable calibrated preview."));
        }
        return;
    }

    const FrameId selectedId(selectedPath);
    const QString selectedCanonicalPath = selectedId.canonicalPath();
    const auto frameOpt = m_model->frames(FrameType::Light).findById(selectedId);
    if (!frameOpt)
    {
        disable(tr("Selected light frame is not available in the current model."));
        return;
    }

    GroupKey lightGroupKey;
    bool groupKeyFound = false;
    const GroupedFrameSet* selectedLightGroup = nullptr;
    for (const auto& group : m_model->lightGroups())
    {
        for (const auto& frame : group.frames)
        {
            if (frame.id.canonicalPath() == selectedCanonicalPath)
            {
                lightGroupKey = group.key;
                groupKeyFound = true;
                selectedLightGroup = &group;
                break;
            }
        }
        if (groupKeyFound)
        {
            break;
        }
    }

    LightCalibrationSettings settings;
    if (groupKeyFound)
    {
        const auto& settingsMap = m_model->lightCalibrationSettings();
        auto it = settingsMap.find(lightGroupKey);
        if (it != settingsMap.end())
        {
            settings = it->second;
        }
    }

    if (settings.preCalibrated)
    {
        const QString groupLabel = groupKeyFound ? lightGroupKey.toCompactString() : QString();
        if (!groupLabel.trimmed().isEmpty())
        {
            disable(tr("This light group (%1) is marked as Pre-Calibrated in the Calibration table, so calibration "
                       "preview is disabled. "
                       "To re-enable, go to the Calibration tab and uncheck Pre-Calibrated for this group.")
                        .arg(groupLabel));
        }
        else
        {
            disable(tr("This light group is marked as Pre-Calibrated in the Calibration table, so calibration preview "
                       "is disabled. "
                       "To re-enable, go to the Calibration tab and uncheck Pre-Calibrated for this group."));
        }
        return;
    }

    CalibrationMatcher matcher = buildConfiguredCalibrationMatcher(m_model.get());

    const FrameInfo& representativeFrame =
        (selectedLightGroup && !selectedLightGroup->frames.empty()) ? selectedLightGroup->frames.front() : *frameOpt;
    const LightGroupKey lightKey = LightGroupKey::fromFrame(representativeFrame);
    const auto temperatureC =
        selectedLightGroup ? averageTemperatureC(selectedLightGroup->frames) : frameOpt->metadata.temperatureC();

    const CalibrationMatch biasMatch = matcher.findBias(lightKey);
    const CalibrationMatch darkMatch = matcher.findDark(lightKey, temperatureC);
    const CalibrationMatch flatMatch = matcher.findFlat(lightKey);

    const auto matchPath = [](const CalibrationMatch& match) -> QString {
        if (match.type == CalibrationMatch::MatchType::None ||
            match.type == CalibrationMatch::MatchType::ManuallyUnassigned)
        {
            return {};
        }
        return match.masterPath;
    };

    QString biasPath = matchPath(biasMatch);
    QString darkPath = matchPath(darkMatch);
    QString flatPath = matchPath(flatMatch);

    const bool hasBias = !biasPath.isEmpty();
    const bool hasDark = !darkPath.isEmpty();
    const bool hasFlat = !flatPath.isEmpty();

    const CalibrationMatchingPolicy policy = m_model->calibrationMatchingPolicy();
    const bool ccdMode = policy.subtractBiasFromDark;

    bool usesBias = hasBias;
    if (hasDark && !ccdMode)
    {
        usesBias = false;
    }

    const bool usesDark = hasDark;
    const bool usesFlat = hasFlat;
    const bool hasAnyCalibration = usesBias || usesDark || usesFlat;

    if (!hasAnyCalibration)
    {
        QStringList manuallyUnassigned;
        if (biasMatch.type == CalibrationMatch::MatchType::ManuallyUnassigned)
        {
            manuallyUnassigned << tr("Bias");
        }
        if (darkMatch.type == CalibrationMatch::MatchType::ManuallyUnassigned)
        {
            manuallyUnassigned << tr("Dark");
        }
        if (flatMatch.type == CalibrationMatch::MatchType::ManuallyUnassigned)
        {
            manuallyUnassigned << tr("Flat");
        }

        if (!manuallyUnassigned.isEmpty())
        {
            disable(tr("Calibration was manually unassigned for this light (%1).")
                        .arg(manuallyUnassigned.join(QStringLiteral(", "))));
        }
        else
        {
            disable(tr("No matching calibration masters were found for this light (bias/dark/flat)."));
        }
        return;
    }

    QStringList missing;
    if (hasDark && ccdMode && !hasBias)
    {
        missing << tr("Bias master is required because CCD mode is enabled (Subtract bias from dark)");
    }

    const auto missingIfAbsent = [&missing](const QString& path, const QString& label) {
        if (path.isEmpty())
        {
            return;
        }
        if (!QFileInfo::exists(path))
        {
            missing
                << QObject::tr("%1 assigned master is not available yet: %2").arg(label, QFileInfo(path).fileName());
        }
    };

    if (usesBias)
    {
        missingIfAbsent(biasPath, tr("Bias"));
    }
    if (usesDark)
    {
        missingIfAbsent(darkPath, tr("Dark"));
    }
    if (usesFlat)
    {
        missingIfAbsent(flatPath, tr("Flat"));
    }

    if (!missing.isEmpty())
    {
        disable(tr("Missing calibration masters: %1. Build the required masters or select existing master files before "
                   "enabling calibrated preview.")
                    .arg(missing.join(QStringLiteral(", "))));
        return;
    }

    if (hasDark && !ccdMode)
    {
        biasPath.clear();
    }

    config.available = true;
    config.tooltip = tr("Preview the selected light with calibration applied.");
    config.biasPath = usesBias ? biasPath : QString();
    config.darkPath = usesDark ? darkPath : QString();
    config.flatPath = usesFlat ? flatPath : QString();
    config.subtractBiasFromDark = ccdMode;
    config.settings.cosmeticCorrection = settings.cosmeticCorrection;
    config.settings.sigmaCold = settings.sigmaCold;
    config.settings.sigmaHot = settings.sigmaHot;
    config.settings.darkOptimization = settings.darkOptimization;
    config.settings.darkMultiplicationFactor = settings.darkMultiplicationFactor;
    config.settings.flatAutoLevel = settings.flatAutoLevel;
    config.settings.pedestal = settings.pedestal;

    preview->setCalibrationPreviewConfig(config);
}

void ImageIntegrationProcess::refreshLightCalibrationPreview()
{
    const int lightIndex = toIndex(LogicalTabIndex::Light);
    FrameSourceTab* tab = frameSourceTabForIndex(lightIndex);
    FrameTreeView* tree = tab ? tab->tree() : nullptr;
    ImageViewControl* preview = tab ? tab->previewPanel() : nullptr;
    if (!tree || !preview)
    {
        return;
    }

    const QStringList paths = tree->selectedFilePaths();
    if (paths.isEmpty())
    {
        preview->setCalibrationPreviewConfig(CalibrationPreviewConfig{});
        return;
    }

    updateLightCalibrationPreview(paths.first());
    if (preview->isCalibrationPreviewEnabled())
    {
        preview->loadImageFromFile(paths.first().toStdString());
    }
}

void ImageIntegrationProcess::setupConnections()
{
    setupUIConnections();
    setupModelConnections();
    setupTreeConnections();
}

void ImageIntegrationProcess::setupUIConnections()
{
    if (outputDirectoryBrowseButton)
    {
        if (const QIcon icon = integrationIcon(QStringLiteral("folder_open")); !icon.isNull())
        {
            outputDirectoryBrowseButton->setIcon(icon);
        }

        connect(outputDirectoryBrowseButton, &QPushButton::clicked, this,
                &ImageIntegrationProcess::onBrowseOutputDirectory);
    }

    if (outputDirectoryEdit)
    {
        connect(outputDirectoryEdit, &QLineEdit::textEdited, this, &ImageIntegrationProcess::onOutputDirectoryEdited);
    }

    if (m_addFilesAction)
    {
        connect(m_addFilesAction, &QAction::triggered, this, &ImageIntegrationProcess::onAddFiles);
    }
    if (m_addDirectoryAction)
    {
        connect(m_addDirectoryAction, &QAction::triggered, this, &ImageIntegrationProcess::onAddDirectory);
    }
    if (m_clearSelectedAction)
    {
        connect(m_clearSelectedAction, &QAction::triggered, this, &ImageIntegrationProcess::onClearSelected);
    }
    if (m_clearAllAction)
    {
        connect(m_clearAllAction, &QAction::triggered, this, &ImageIntegrationProcess::onClearAll);
    }
    if (m_resetAction)
    {
        connect(m_resetAction, &QAction::triggered, this, &ImageIntegrationProcess::onReset);
    }
    if (m_importProjectAction)
    {
        connect(m_importProjectAction, &QAction::triggered, this, &ImageIntegrationProcess::onImportProject);
    }
    if (m_exportProjectAction)
    {
        connect(m_exportProjectAction, &QAction::triggered, this, &ImageIntegrationProcess::onExportProject);
    }
    if (m_executeAction)
    {
        connect(m_executeAction, &QAction::triggered, this, &ImageIntegrationProcess::onExecute);
    }

    if (m_registrationTab)
    {
        connect(m_registrationTab, &RegistrationTab::runRegistrationRequested, this,
                &ImageIntegrationProcess::onRunRegistrationForGroup);

        connect(m_registrationTab, &RegistrationTab::runRegistrationAllRequested, this,
                &ImageIntegrationProcess::onRunRegistrationForAllGroups);
    }

    if (m_stackingTab)
    {
        connect(m_stackingTab, &StackingTab::runIntegrationRequested, this,
                &ImageIntegrationProcess::onRunIntegrationForGroup);

        connect(m_stackingTab, &StackingTab::runIntegrationAllRequested, this, &ImageIntegrationProcess::onExecute);
    }

    // Connect SummaryTab signals
    if (m_summaryTab)
    {
        connect(m_summaryTab, &SummaryTab::fixRequested, this, &ImageIntegrationProcess::onFixRequested);
        connect(m_summaryTab, &SummaryTab::fixWithGroupRequested, this,
                &ImageIntegrationProcess::onFixWithGroupRequested);
        connect(m_summaryTab, &SummaryTab::executeRequested, this, &ImageIntegrationProcess::onExecute);

        connect(m_summaryTab, &SummaryTab::openRequested, this, [this](const QString& path) {
            const QString trimmed = path.trimmed();
            if (trimmed.isEmpty())
            {
                return;
            }

            process_utils::openRecentSingleFile(trimmed);
        });

        connect(m_summaryTab, &SummaryTab::compareRequested, this,
                [this](const QStringList& paths, const QStringList& labels, const QStringList& contexts,
                       const QVariantList& historyRows, const QString& groupKey) {
                    if (paths.size() < 2)
                    {
                        openNonBlockingMessageBox(this, QMessageBox::Information, tr("Compare Masters"),
                                                  tr("Select at least two stacked masters to compare."));
                        return;
                    }

                    auto* dialog = new BlinkComparatorDialog(paths, labels, contexts, this);
                    dialog->setAttribute(Qt::WA_DeleteOnClose);
                    dialog->setWindowTitle(groupKey.trimmed().isEmpty()
                                               ? tr("Compare Stacked Masters")
                                               : tr("Compare Stacked Masters - %1").arg(groupKey.trimmed()));
                    dialog->setHistoryRows(historyRows);
                    dialog->setMode(BlinkComparatorDialog::BlinkMode::SideBySide);
                    dialog->open();
                });

        connect(m_summaryTab, &SummaryTab::exportReportRequested, this, [this](const QString& runDir) {
            const QString trimmed = runDir.trimmed();
            if (trimmed.isEmpty())
            {
                return;
            }

            const QString manifestPath = QDir(trimmed).filePath(QStringLiteral("workflow_manifest.json"));
            QFile file(manifestPath);
            if (!file.open(QIODevice::ReadOnly))
            {
                openNonBlockingMessageBox(this, QMessageBox::Warning, tr("Export Report"),
                                          tr("Could not open workflow manifest: %1").arg(manifestPath));
                return;
            }

            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (!doc.isObject())
            {
                openNonBlockingMessageBox(this, QMessageBox::Warning, tr("Export Report"),
                                          tr("Invalid workflow manifest: %1").arg(manifestPath));
                return;
            }

            const QJsonObject root = doc.object();
            if (!root.value(QStringLiteral("manifest")).isObject())
            {
                openNonBlockingMessageBox(
                    this, QMessageBox::Warning, tr("Export Report"),
                    tr("Workflow archive is missing embedded manifest data: %1").arg(manifestPath));
                return;
            }

            // The report reads the run's images and headers through the session, so it is built on the thread pool and
            // only what it wrote is opened on the GUI thread.
            const QPointer<ImageIntegrationProcess> self(this);
            const QString title = tr("AfterNight Workflow Report");
            (void)QtConcurrent::run([self, trimmed, root, manifestPath, title]() {
                struct Written
                {
                    QString error;
                    QString opened;
                };
                Written written;
                const QString reportPath = QDir(trimmed).filePath(QStringLiteral("workflow_report.md"));
                const QString md = ImageIntegration::Reporting::buildWorkflowReportMarkdownFromArchive(
                    trimmed, root, ImageIntegration::detail::applicationEngineSession());
                QFile reportFile(reportPath);
                if (md.trimmed().isEmpty())
                {
                    written.error = QCoreApplication::translate("ImageIntegrationProcess",
                                                                "Failed to build workflow report from manifest: %1")
                                        .arg(manifestPath);
                }
                else if (!reportFile.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
                         reportFile.write(md.toUtf8()) < 0)
                {
                    written.error = QCoreApplication::translate("ImageIntegrationProcess", "Could not write report: %1")
                                        .arg(reportPath);
                }
                else
                {
                    reportFile.close();
                    const QString htmlPath = QDir(trimmed).filePath(QStringLiteral("workflow_report.html"));
                    QString htmlError;
                    const bool htmlOk =
                        ImageIntegration::Reporting::writeStyledHtmlFromMarkdown(md, htmlPath, title, &htmlError);
                    if (!htmlOk)
                    {
                        LOG_WARNING("ImageIntegrationProcess: Failed to export HTML report '{}': {}",
                                    htmlPath.toStdString(), htmlError.toStdString());
                    }
                    written.opened = htmlOk ? htmlPath : reportPath;
                }
                QMetaObject::invokeMethod(
                    qApp,
                    [self, written]() {
                        if (!self)
                        {
                            return;
                        }
                        if (!written.error.isEmpty())
                        {
                            openNonBlockingMessageBox(
                                self, QMessageBox::Warning,
                                QCoreApplication::translate("ImageIntegrationProcess", "Export Report"), written.error);
                            return;
                        }
                        an::ui::openLocalFile(written.opened);
                        if (self->m_summaryTab)
                        {
                            self->m_summaryTab->refreshAll();
                        }
                    },
                    Qt::QueuedConnection);
            });
        });

        connect(m_summaryTab, &SummaryTab::rerunRequested, this, [this](const QString& runDir) {
            const QString trimmed = runDir.trimmed();
            if (trimmed.isEmpty())
            {
                return;
            }

            const QString manifestPath = QDir(trimmed).filePath(QStringLiteral("workflow_manifest.json"));
            QFile file(manifestPath);
            if (!file.open(QIODevice::ReadOnly))
            {
                openNonBlockingMessageBox(this, QMessageBox::Warning, tr("Re-run"),
                                          tr("Could not open workflow manifest: %1").arg(manifestPath));
                return;
            }

            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (!doc.isObject())
            {
                openNonBlockingMessageBox(this, QMessageBox::Warning, tr("Re-run"),
                                          tr("Invalid workflow manifest: %1").arg(manifestPath));
                return;
            }

            const QJsonObject root = doc.object();
            const QJsonObject embeddedManifest = root.value(QStringLiteral("manifest")).toObject();
            if (embeddedManifest.isEmpty())
            {
                openNonBlockingMessageBox(
                    this, QMessageBox::Warning, tr("Re-run"),
                    tr("Workflow archive does not include an embedded manifest: %1").arg(manifestPath));
                return;
            }

            WorkflowManifest archivedManifest = WorkflowManifest::fromJson(embeddedManifest);
            if (!archivedManifest.isCurrentFormat())
            {
                openNonBlockingMessageBox(this, QMessageBox::Warning, tr("Re-run"),
                                          tr("The workflow manifest in %1 %2; this version of AfterNight reads format "
                                             "%3, and the run was not started.")
                                              .arg(QDir::toNativeSeparators(manifestPath),
                                                   archivedManifest.version > 0
                                                       ? tr("was written in format %1").arg(archivedManifest.version)
                                                       : tr("states no format version"))
                                              .arg(WorkflowManifest::kFormatVersion));
                return;
            }
            archivedManifest.executionId.clear();
            if (!m_model->outputDirectory().trimmed().isEmpty())
            {
                archivedManifest.outputDirectory = m_model->outputDirectory();
            }
            if (!checkOutputDirectoryBeforeRun(this, archivedManifest.outputDirectory))
            {
                return;
            }
            WorkflowExecutionController executionController(m_model.get(), this);
            const QPointer<ImageIntegrationProcess> self(this);
            const bool launched =
                executionController.runAsyncValidated(archivedManifest, [self](const WorkflowExecutor::Result&) {
                    if (self && self->m_summaryTab)
                    {
                        self->m_summaryTab->refreshAll();
                    }
                });
            if (!launched && m_summaryTab)
            {
                m_summaryTab->refreshAll();
            }
        });
    }

    // Track most recently used frame tab for actions invoked from non-frame tabs.
    if (sourceTabWidget)
    {
        connect(sourceTabWidget, &QTabWidget::currentChanged, this, [this](int index) {
            if (index >= 0 && index < kFrameTabCount)
            {
                m_lastFrameTabIndex = index;

                // Ensure the tab's grouping widget reflects any model changes.
                if (auto* tab = frameSourceTabForIndex(index))
                {
                    tab->refreshGroupingOptionsFromModel();
                }
            }
        });
    }

    if (dialogButtonBox)
    {
        connect(dialogButtonBox, &QDialogButtonBox::rejected, this, &ImageIntegrationProcess::close);
        if (auto* resetBtn = dialogButtonBox->button(QDialogButtonBox::Reset))
        {
            connect(resetBtn, &QPushButton::clicked, this, &ImageIntegrationProcess::onReset);
        }
    }
}

void ImageIntegrationProcess::setupModelConnections()
{
    if (!m_model)
    {
        return;
    }

    const auto refreshCalibrationPreview = [this]() {
        FrameSourceTab* tab = frameSourceTabForIndex(indexFor(FrameType::Light));
        if (tab)
        {
            if (auto* preview = tab->previewPanel())
            {
                preview->clearCalibrationCaches();
            }
        }
        refreshLightCalibrationPreview();
    };

    connect(m_model.get(), &IntegrationDataModel::outputDirectoryChanged, this,
            &ImageIntegrationProcess::onOutputDirectoryChanged);

    connect(m_model.get(), &IntegrationDataModel::framesAdded, this, &ImageIntegrationProcess::updateButtonStates);
    connect(m_model.get(), &IntegrationDataModel::framesRemoved, this, &ImageIntegrationProcess::updateButtonStates);
    connect(m_model.get(), &IntegrationDataModel::modelReset, this, &ImageIntegrationProcess::updateButtonStates);

    connect(m_model.get(), &IntegrationDataModel::framesAdded, this,
            [refreshCalibrationPreview](FrameType type, const QStringList&) {
                if (type == FrameType::Bias || type == FrameType::Dark || type == FrameType::Flat ||
                    type == FrameType::Light)
                {
                    refreshCalibrationPreview();
                }
            });
    connect(m_model.get(), &IntegrationDataModel::framesRemoved, this,
            [refreshCalibrationPreview](FrameType type, const QStringList&) {
                if (type == FrameType::Bias || type == FrameType::Dark || type == FrameType::Flat ||
                    type == FrameType::Light)
                {
                    refreshCalibrationPreview();
                }
            });
    connect(m_model.get(), &IntegrationDataModel::frameUpdated, this,
            [refreshCalibrationPreview](const QString&) { refreshCalibrationPreview(); });
    connect(m_model.get(), &IntegrationDataModel::groupingChanged, this,
            [refreshCalibrationPreview](FrameType) { refreshCalibrationPreview(); });
    connect(m_model.get(), &IntegrationDataModel::calibrationChanged, this,
            [refreshCalibrationPreview](FrameType) { refreshCalibrationPreview(); });
    connect(m_model.get(), &IntegrationDataModel::settingsChanged, this,
            [refreshCalibrationPreview]() { refreshCalibrationPreview(); });
    connect(m_model.get(), &IntegrationDataModel::outputDirectoryChanged, this,
            [refreshCalibrationPreview](const QString&) { refreshCalibrationPreview(); });
    connect(m_model.get(), &IntegrationDataModel::modelReset, this,
            [refreshCalibrationPreview]() { refreshCalibrationPreview(); });

    connect(m_model.get(), &IntegrationDataModel::framesAdded, this,
            [this](FrameType, const QStringList&) { updateWindowTitle(); });
    connect(m_model.get(), &IntegrationDataModel::framesRemoved, this,
            [this](FrameType, const QStringList&) { updateWindowTitle(); });
    connect(m_model.get(), &IntegrationDataModel::settingsChanged, this, [this]() { updateWindowTitle(); });
    connect(m_model.get(), &IntegrationDataModel::modelReset, this, [this]() { updateWindowTitle(); });

    connect(m_model.get(), &IntegrationDataModel::regroupResetSettings, this, [this](FrameType type) {
        const int idx = indexFor(type);
        FrameSourceTab* tab = frameSourceTabForIndex(idx);
        QLabel* label = tab ? tab->regroupWarningLabel() : nullptr;
        if (!label)
        {
            return;
        }

        label->setText(tr("Grouping changed for %1: per-group settings were reset.").arg(toString(type)));
        label->setVisible(true);

        // Auto-hide after 5 seconds
        QTimer::singleShot(5000, label, [label]() {
            if (label)
            {
                label->setVisible(false);
            }
        });
    });

    // Connect model selection changes for cross-table sync
    connect(m_model.get(), &IntegrationDataModel::selectionChanged, this,
            &ImageIntegrationProcess::syncSelectionFromModel);

    // Tab badges are driven by validation state.
    connect(m_model.get(), &IntegrationDataModel::framesAdded, this, [this](FrameType, const QStringList&) {
        scheduleTabBadgeRefresh();
        updateFrameTabLabels();
    });
    connect(m_model.get(), &IntegrationDataModel::framesRemoved, this, [this](FrameType, const QStringList&) {
        scheduleTabBadgeRefresh();
        updateFrameTabLabels();
    });
    connect(m_model.get(), &IntegrationDataModel::groupingChanged, this,
            [this](FrameType) { scheduleTabBadgeRefresh(); });
    connect(m_model.get(), &IntegrationDataModel::calibrationChanged, this,
            [this](FrameType) { scheduleTabBadgeRefresh(); });
    connect(m_model.get(), &IntegrationDataModel::registrationChanged, this, [this]() { scheduleTabBadgeRefresh(); });
    connect(m_model.get(), &IntegrationDataModel::integrationGroupsChanged, this,
            [this]() { scheduleTabBadgeRefresh(); });
    connect(m_model.get(), &IntegrationDataModel::settingsChanged, this, [this]() { scheduleTabBadgeRefresh(); });
    connect(m_model.get(), &IntegrationDataModel::outputDirectoryChanged, this,
            [this](const QString&) { scheduleTabBadgeRefresh(); });
    connect(m_model.get(), &IntegrationDataModel::modelReset, this, [this]() {
        scheduleTabBadgeRefresh();
        updateFrameTabLabels();
    });
}

void ImageIntegrationProcess::setupTreeConnections()
{
    // Connect trees to per-tab preview and context menu
    for (int i = 0; i < kFrameTabCount; ++i)
    {
        if (!m_frameTrees[i])
            continue;

        connect(m_frameTrees[i], &FrameTreeView::frameSelectionChanged, this, [this, i]() {
            auto paths = m_frameTrees[i]->selectedFilePaths();
            const FrameType type = frameTypeForIndex(i);
            FrameSourceTab* tab = frameSourceTabForIndex(i);
            ImageViewControl* preview = tab ? tab->previewPanel() : nullptr;

            if (preview)
            {
                if (type == FrameType::Light)
                {
                    updateLightCalibrationPreview(paths.isEmpty() ? QString() : paths.first());
                }
                else
                {
                    preview->setCalibrationPreviewConfig(CalibrationPreviewConfig{});
                }
            }

            if (!paths.isEmpty())
            {
                if (preview)
                {
                    preview->loadImageFromFile(paths.first().toStdString());
                }
            }
            else
            {
                if (preview)
                {
                    preview->clear();
                }
            }
            updateButtonStates();

            // Sync selection to model for cross-table sync
            syncSelectionToModel(i);
        });

        connect(m_frameTrees[i], &FrameTreeView::filesDropped, this, [this, i](const QStringList& paths) {
            FrameType currentType = frameTypeForIndex(i);
            std::vector<ImageIntegration::DetectedFrameInfo> detectedFrames;

            ImageIntegration::ImportResult importResult;
            int importedCount = 0;

            const bool forceToTab = (m_frameTrees[i]->lastDropModifiers() & Qt::ShiftModifier);

            QStringList expandedPaths;
            QSet<QString> seenPaths;
            for (const auto& p : paths)
            {
                const QFileInfo info(p);
                if (info.isDir())
                {
                    QDirIterator it(p, QDir::Files, QDirIterator::Subdirectories);
                    while (it.hasNext())
                    {
                        const QString filePath = it.next();
                        if (!seenPaths.contains(filePath))
                        {
                            seenPaths.insert(filePath);
                            expandedPaths << filePath;
                        }
                    }
                    continue;
                }

                if (!seenPaths.contains(p))
                {
                    seenPaths.insert(p);
                    expandedPaths << p;
                }
            }

            // A drop is read on the GUI thread, so its probes take a thread only when one is free.
            const an::consumer::EngineSession* engineSession = ImageIntegration::detail::applicationEngineSession();
            for (const auto& p : expandedPaths)
            {
                if (engineSession != nullptr && ImageIntegration::detail::isReadableImagePath(p))
                {
                    const auto res = ImageIntegration::buildDetectedFrameInfoWithIssue(
                        *engineSession, p, currentType, ImageIntegration::ImportAdmission::NoWait);
                    if (res.issue)
                    {
                        importResult.issues.push_back(*res.issue);
                    }
                    if (res.detected)
                    {
                        ++importedCount;
                        detectedFrames.push_back(std::move(*res.detected));
                    }
                }
            }

            importResult.importedCount = importedCount;
            importResult.skippedCount = static_cast<int>(expandedPaths.size()) - importedCount;

            if (importResult.hasIssues())
            {
                const QString title = tr("Import Issues");
                const QString summary = importResult.summaryMessage();
                if (importResult.hasErrors())
                {
                    openNonBlockingMessageBox(this, QMessageBox::Warning, title, summary, importResult.detailedText());
                }
                else
                {
                    notifyWarnOrDialog(this, title, summary);
                }
            }

            PreparedImportBatch batch = prepareImportBatch(std::move(detectedFrames), currentType, forceToTab);
            if (!batch.groupedInfos.empty())
            {
                applyImportBatch(m_model.get(), m_undoStack, batch, tr("Import frames"));

                const QString title = forceToTab ? tr("Import Complete (Forced to Tab)") : tr("Import Complete");
                const QString summary = importSummaryText(batch.groupedInfos, importResult.skippedCount);
                notifyInfoOrDialog(this, title, summary);
            }
        });

        // Connect context menu
        connect(m_frameTrees[i], &FrameTreeView::contextMenuRequested, this,
                [this, i](const QPoint& pos, const QStringList& /*selectedPaths*/) {
                    FrameType type = frameTypeForIndex(i);

                    FrameTreeView* view = m_frameTrees[i];
                    QModelIndexList selectedIndices;

                    if (view && view->selectionModel())
                    {
                        // Prefer the item under the cursor over any previous selection.
                        const QPoint localPos = view->viewport()->mapFromGlobal(pos);
                        QModelIndex hit = view->indexAt(localPos);
                        if (!hit.isValid())
                        {
                            // Allow right-click on the empty horizontal space of a row.
                            hit = view->indexAt(QPoint(0, localPos.y()));
                        }

                        const QModelIndex hitName =
                            hit.isValid() ? hit.sibling(hit.row(), GroupTreeModel::ColName) : QModelIndex();
                        auto* model = dynamic_cast<GroupTreeModel*>(view->model());

                        if (model && hitName.isValid())
                        {
                            if (model->isGroup(hitName))
                            {
                                selectedIndices = {hitName};
                            }
                            else if (model->isFrame(hitName))
                            {
                                // If the clicked frame is part of the current multi-selection, keep it.
                                const QModelIndexList selectedRows =
                                    view->selectionModel()->selectedRows(GroupTreeModel::ColName);
                                bool hitInSelection = false;
                                for (const QModelIndex& idx : selectedRows)
                                {
                                    if (idx == hitName)
                                    {
                                        hitInSelection = true;
                                        break;
                                    }
                                }

                                selectedIndices =
                                    hitInSelection ? view->selectionModel()->selectedRows() : QModelIndexList{hitName};
                            }
                        }

                        if (selectedIndices.isEmpty())
                        {
                            selectedIndices = view->selectionModel()->selectedRows();
                        }
                    }

                    m_contextMenuHandler->showContextMenu(view, type, pos, selectedIndices);
                });
    }

    // Connect context menu handler signals
    if (m_contextMenuHandler)
    {
        connect(m_contextMenuHandler, &TreeContextMenuHandler::removeFramesRequested, this,
                [this](FrameType type, const QStringList& paths) {
                    if (!m_undoStack || !m_model || paths.isEmpty())
                    {
                        return;
                    }

                    auto frames = collectFramesForUndo(m_model.get(), type, paths);
                    if (frames.empty())
                    {
                        return;
                    }
                    m_undoStack->push(new RemoveFramesCommand(m_model.get(), type, std::move(frames)));
                });

        connect(m_contextMenuHandler, &TreeContextMenuHandler::frameTypeChangeRequested, this,
                &ImageIntegrationProcess::changeFrameType);

        connect(m_contextMenuHandler, &TreeContextMenuHandler::masterStatusChangeRequested, this,
                [this](const QString& path, bool isMaster) {
                    if (!m_undoStack || !m_model)
                    {
                        return;
                    }

                    const FrameId id(path);
                    const bool oldIsMaster = m_model->isMasterFrame(id);
                    const MasterSource oldSource = m_model->masterSource(id).value_or(MasterSource::Unknown);
                    const MasterSource newSource = isMaster ? MasterSource::UserDefined : oldSource;
                    m_undoStack->push(new SetMasterStatusCommand(m_model.get(), id.canonicalPath(), oldIsMaster,
                                                                 oldSource, isMaster, newSource));
                });

        connect(
            m_contextMenuHandler, &TreeContextMenuHandler::referenceFrameRequested, this, [this](const QString& path) {
                if (!m_undoStack || !m_model)
                {
                    return;
                }
                auto frameOpt = m_model->frames(FrameType::Light).findById(FrameId(path));
                if (!frameOpt)
                    return;
                RegistrationGroupKey key = RegistrationGroupKey::fromFrame(*frameOpt);
                auto oldReference = m_model->registrationReferenceFrame(key);
                m_undoStack->push(new SetRegistrationReferenceCommand(m_model.get(), key, oldReference, frameOpt->id));
            });

        connect(m_contextMenuHandler, &TreeContextMenuHandler::blinkRequested, this,
                [this](FrameType frameType, const QStringList& paths) {
                    auto* dialog = new BlinkComparatorDialog(paths, this);
                    dialog->setAttribute(Qt::WA_DeleteOnClose);
                    const QPointer<BlinkComparatorDialog> dialogPtr(dialog);
                    connect(dialog, &QDialog::finished, this, [this, dialogPtr, frameType]() {
                        if (!dialogPtr)
                        {
                            return;
                        }

                        // false = no exclusions to apply (not an error); changes are undo-aware when m_undoStack is
                        // set.
                        (void)ImageIntegration::blink::applyExclusions(m_model.get(), m_undoStack, frameType,
                                                                       dialogPtr->comparisonResult().excludedPaths);
                    });
                    dialog->open();
                });

        connect(m_contextMenuHandler, &TreeContextMenuHandler::expandAllRequested, this, [this]() {
            const int index = currentFrameTabIndex();
            if (index >= 0 && index < kFrameTabCount && m_frameTrees[index])
            {
                m_frameTrees[index]->expandAll();
            }
        });

        connect(m_contextMenuHandler, &TreeContextMenuHandler::collapseAllRequested, this, [this]() {
            const int index = currentFrameTabIndex();
            if (index >= 0 && index < kFrameTabCount && m_frameTrees[index])
            {
                m_frameTrees[index]->collapseAll();
            }
        });

        connect(m_contextMenuHandler, &TreeContextMenuHandler::showInCalibrationTableRequested, this,
                [this](FrameType frameType, const QString& groupKeyStr) {
                    if (!m_calibrationTab)
                    {
                        return;
                    }

                    const auto keyOpt = GroupKey::fromString(groupKeyStr);
                    if (!keyOpt.has_value())
                    {
                        return;
                    }

                    setCurrentLogicalIndex(4); // Calibration
                    m_calibrationTab->showGroupInTable(frameType, *keyOpt);
                });

        connect(m_contextMenuHandler, &TreeContextMenuHandler::showInRegistrationTableRequested, this,
                [this](const QString& framePath) {
                    if (!m_registrationTab)
                    {
                        return;
                    }

                    setCurrentLogicalIndex(5); // Registration
                    m_registrationTab->showFrameInTable(framePath);
                });

        connect(m_contextMenuHandler, &TreeContextMenuHandler::showInIntegrationTableRequested, this,
                [this](const QString& framePath) {
                    if (!m_stackingTab)
                    {
                        return;
                    }

                    setCurrentLogicalIndex(6); // Stacking / Integration
                    m_stackingTab->showFrameInTable(framePath);
                });
    }

    if (m_model)
    {
        connect(m_model.get(), &IntegrationDataModel::masterSelectionPolicyWarning, this,
                [this](const QString& title, const QString& text) { notifyWarnOrDialog(this, title, text); });
    }

    if (m_dragDropHandler)
    {
        connect(m_dragDropHandler, &FrameTreeDragDropHandler::frameTypeChangeRequested, this,
                &ImageIntegrationProcess::changeFrameType);
    }
}

int ImageIntegrationProcess::currentFrameTabIndex() const
{
    const int current = currentLogicalIndex();
    if (current >= 0 && current < kFrameTabCount)
    {
        return current;
    }

    return m_lastFrameTabIndex;
}

FrameType ImageIntegrationProcess::currentFrameTypeForActions() const
{
    return frameTypeForIndex(currentFrameTabIndex());
}

FrameSourceTab* ImageIntegrationProcess::frameSourceTabForIndex(int index) const
{
    if (index < 0 || index >= kFrameTabCount)
    {
        return nullptr;
    }
    return m_frameSourceTabs[static_cast<size_t>(index)];
}

void ImageIntegrationProcess::applySplitterDefaults()
{
    // No-op: the current UI no longer uses splitters.
}

void ImageIntegrationProcess::onBrowseOutputDirectory()
{
    const QString initial = outputDirectoryEdit ? outputDirectoryEdit->text().trimmed() : QString();
    const QString selected = process_utils::selectExistingDirectory(this, tr("Select Output Directory"), initial);
    if (selected.trimmed().isEmpty())
    {
        return;
    }

    if (m_model && m_undoStack)
    {
        const QString oldPath = m_model->outputDirectory();
        if (oldPath != selected)
        {
            m_undoStack->push(new SetOutputDirectoryCommand(m_model.get(), oldPath, selected));
        }
    }
    else if (m_model)
    {
        m_model->setOutputDirectory(selected);
    }
}

void ImageIntegrationProcess::onOutputDirectoryEdited(const QString& text)
{
    if (m_ignoreOutputUpdates || !m_model)
    {
        return;
    }

    if (!m_undoStack)
    {
        m_model->setOutputDirectory(text.trimmed());
        return;
    }

    const QString newPath = text.trimmed();
    const QString oldPath = m_model->outputDirectory();
    if (newPath != oldPath)
    {
        m_undoStack->push(new SetOutputDirectoryCommand(m_model.get(), oldPath, newPath));
    }
}

void ImageIntegrationProcess::onOutputDirectoryChanged(const QString& path)
{
    if (!outputDirectoryEdit)
    {
        return;
    }

    QSignalBlocker blocker(outputDirectoryEdit);
    outputDirectoryEdit->setText(path);
    updateButtonStates();
    refreshAfterOutputDirectoryChange();
}

void ImageIntegrationProcess::refreshAfterOutputDirectoryChange()
{
    if (m_model)
    {
        const QString outputDir = m_model->outputDirectory().trimmed();
        if (!outputDir.isEmpty())
        {
            (void)WorkflowArchiver::cleanupOrphanedStagedFiles(outputDir);
        }
    }

    for (auto* tab : m_frameSourceTabs)
    {
        if (!tab)
        {
            continue;
        }
        tab->refreshGroupingOptionsFromModel();
        tab->refreshQuickStats();
        tab->refreshFilterStats();
    }

    if (m_calibrationTab)
    {
        m_calibrationTab->refreshAll();
    }
    if (m_registrationTab)
    {
        m_registrationTab->refreshAll();
    }
    if (m_stackingTab)
    {
        m_stackingTab->refreshAll();
    }
    if (m_summaryTab)
    {
        m_summaryTab->refreshAll();
    }
}

void ImageIntegrationProcess::onClearSelected()
{
    const int index = currentFrameTabIndex();
    if (index < 0 || index >= kFrameTabCount || !m_frameTrees[index])
    {
        return;
    }

    const FrameType type = frameTypeForIndex(index);
    const QStringList selected = m_frameTrees[index]->selectedFilePaths();

    if (selected.isEmpty())
    {
        return;
    }

    const auto removeSelectedFrames = [self = QPointer<ImageIntegrationProcess>(this), type,
                                       selected](const bool skipFutureConfirmation) {
        auto* process = self.data();
        if (!process || !process->m_model || !process->m_undoStack)
        {
            return;
        }

        if (skipFutureConfirmation)
        {
            process->m_skipRemoveConfirmationForSession = true;
        }

        std::vector<FrameInfo> toRemove;
        for (const auto& path : selected)
        {
            if (auto frame = process->m_model->frames(type).findById(FrameId(path)))
            {
                toRemove.push_back(*frame);
            }
        }
        if (!toRemove.empty())
        {
            process->m_undoStack->push(new RemoveFramesCommand(process->m_model.get(), type, std::move(toRemove)));
        }
    };

    if (!ImageIntegration::shouldShowRemoveFramesConfirmation(selected.size(), m_skipRemoveConfirmationForSession))
    {
        removeSelectedFrames(false);
        return;
    }

    openNonBlockingQuestionBox(
        this, tr("Remove Frames"),
        tr("Removing frames from this tab.\n\n%1")
            .arg(ImageIntegration::buildRemoveFramesConfirmationMessage(selected)),
        [removeSelectedFrames](const QMessageBox::StandardButton button, const bool skipFutureConfirmation) {
            if (button == QMessageBox::Yes)
            {
                removeSelectedFrames(skipFutureConfirmation);
            }
        },
        tr("Don't ask again for this session"));
}

void ImageIntegrationProcess::onClearAllInTab(FrameType type)
{
    if (!m_model || !m_undoStack)
    {
        return;
    }

    auto frames = m_model->frames(type).frames();
    if (frames.empty())
    {
        return;
    }

    const int count = static_cast<int>(frames.size());
    auto removeFrames = [self = QPointer<ImageIntegrationProcess>(this), type,
                         frames](const bool skipFutureConfirmation) mutable {
        auto* process = self.data();
        if (!process || !process->m_model || !process->m_undoStack || frames.empty())
        {
            return;
        }

        if (skipFutureConfirmation)
        {
            process->m_skipRemoveConfirmationForSession = true;
        }

        process->m_undoStack->push(new RemoveFramesCommand(process->m_model.get(), type, std::move(frames)));
    };

    if (!ImageIntegration::shouldShowRemoveFramesConfirmation(count, m_skipRemoveConfirmationForSession))
    {
        removeFrames(false);
        return;
    }

    QString typeLabel;
    switch (type)
    {
    case FrameType::Bias:
        typeLabel = tr("Bias");
        break;
    case FrameType::Dark:
        typeLabel = tr("Darks");
        break;
    case FrameType::Flat:
        typeLabel = tr("Flats");
        break;
    case FrameType::Light:
    default:
        typeLabel = tr("Lights");
        break;
    }

    openNonBlockingQuestionBox(
        this, tr("Remove Frames"),
        tr("Removing frames from this tab.\n\nAre you sure you want to remove all %1 frame(s) from the "
           "%2 tab?\n\nYou can undo this action with Ctrl+Z.")
            .arg(count)
            .arg(typeLabel),
        [removeFrames](const QMessageBox::StandardButton button, const bool skipFutureConfirmation) mutable {
            if (button == QMessageBox::Yes)
            {
                removeFrames(skipFutureConfirmation);
            }
        },
        tr("Don't ask again for this session"));
}

void ImageIntegrationProcess::onClearAll()
{
    if (!m_model || !m_undoStack)
    {
        return;
    }

    // Collect all frames from all types before confirmation so the undo command matches the prompt.
    std::vector<FrameInfo> allFrames;
    for (const auto& f : m_model->biasFrames().frames())
    {
        allFrames.push_back(f);
    }
    for (const auto& f : m_model->darkFrames().frames())
    {
        allFrames.push_back(f);
    }
    for (const auto& f : m_model->flatFrames().frames())
    {
        allFrames.push_back(f);
    }
    for (const auto& f : m_model->lightFrames().frames())
    {
        allFrames.push_back(f);
    }

    if (allFrames.empty())
    {
        return;
    }

    openNonBlockingQuestionBox(
        this, tr("Clear All"), tr("Removing frames from all tabs.\n\nAre you sure you want to remove all frames?"),
        [self = QPointer<ImageIntegrationProcess>(this),
         allFrames = std::move(allFrames)](const QMessageBox::StandardButton button, bool) mutable {
            auto* process = self.data();
            if (button != QMessageBox::Yes || !process || !process->m_model || !process->m_undoStack)
            {
                return;
            }

            process->m_undoStack->push(new ClearAllCommand(process->m_model.get(), std::move(allFrames)));
        });
}

void ImageIntegrationProcess::onReset()
{
    if (!m_model)
    {
        return;
    }

    openNonBlockingQuestionBox(
        this, tr("Reset Settings"),
        tr("Are you sure you want to reset all settings to their defaults?\n\n"
           "Loaded frames and output directory will be preserved.\n"
           "All other configuration (grouping, calibration, registration, stacking) will be lost."),
        [self = QPointer<ImageIntegrationProcess>(this)](const QMessageBox::StandardButton button, bool) {
            auto* process = self.data();
            if (button != QMessageBox::Yes || !process || !process->m_model)
            {
                return;
            }

            // Preserve frames and output directory
            const QString outputDir = process->m_model->outputDirectory();
            const auto biasFrames = process->m_model->frames(FrameType::Bias).frames();
            const auto darkFrames = process->m_model->frames(FrameType::Dark).frames();
            const auto flatFrames = process->m_model->frames(FrameType::Flat).frames();
            const auto lightFrames = process->m_model->frames(FrameType::Light).frames();

            process->m_model->reset();

            // Restore preserved state
            process->m_model->setOutputDirectory(outputDir);

            process->m_model->beginBatchUpdate();
            if (!biasFrames.empty())
            {
                process->m_model->addFrames(FrameType::Bias, biasFrames);
            }
            if (!darkFrames.empty())
            {
                process->m_model->addFrames(FrameType::Dark, darkFrames);
            }
            if (!flatFrames.empty())
            {
                process->m_model->addFrames(FrameType::Flat, flatFrames);
            }
            if (!lightFrames.empty())
            {
                process->m_model->addFrames(FrameType::Light, lightFrames);
            }
            process->m_model->endBatchUpdate();

            process->autoDetectMasters();

            if (process->m_undoStack)
            {
                process->m_undoStack->clear();
            }

            // Reset local UI state
            process->setCurrentLogicalIndex(0);
        });
}

void ImageIntegrationProcess::autoDetectMasters()
{
    if (!m_model)
    {
        return;
    }

    const std::array<FrameType, 3> types = {FrameType::Bias, FrameType::Dark, FrameType::Flat};
    for (const auto type : types)
    {
        const auto& collection = m_model->frames(type);
        if (collection.size() == 1)
        {
            const auto frames = collection.frames();
            const auto& frame = frames.front();
            if (ImageIntegration::isProbablyMasterFilePath(frame.displayPath))
            {
                m_model->setMasterStatus(frame.id.canonicalPath(), true, MasterSource::AutoDetected);
            }
        }
    }
}

void ImageIntegrationProcess::onImportProject()
{
    static const QString contextKey = QStringLiteral("__IMAGE_INTEGRATION_PROJECT__");
    const QString initialDir = rememberedExistingPath(contextKey);

    const QString file = an::ui::dialogs::getOpenFileName(
        process_utils::currentGlobalSettings(), this, tr("Import Project"), initialDir, tr("Project Files (*.json)"));
    if (file.isEmpty())
    {
        return;
    }

    rememberDirectoryForPath(contextKey, file);

    onOpenRecentProject(file);
}

void ImageIntegrationProcess::onOpenRecentProject(const QString& projectPath)
{
    const QString file = projectPath.trimmed();
    if (file.isEmpty())
    {
        return;
    }

    if (!QFileInfo::exists(file))
    {
        openNonBlockingMessageBox(this, QMessageBox::Warning, tr("Open Recent"),
                                  tr("Project file not found:\n%1").arg(file));

        if (m_projectManager)
        {
            m_projectManager->removeRecentProject(file);
            persistRecentProjects();
            refreshRecentProjectsMenu();
        }
        return;
    }

    // A project file is a stored session document, read strictly: one it refuses loads nothing rather than part.
    const ProjectReadResult read = ProjectManager::readProject(file);
    if (!read.model.has_value())
    {
        LOG_WARNING("ImageIntegrationProcess: project file '{}' was refused: {}", file.toStdString(),
                    read.refusal.toStdString());
        openNonBlockingMessageBox(
            this, QMessageBox::Warning, tr("Import Failed"),
            tr("The project file could not be read, and nothing was loaded.\n\n%1").arg(read.refusal));
        return;
    }

    m_model->fromJson(*read.model);
    if (m_projectManager)
    {
        m_projectManager->setProjectName(QFileInfo(file).completeBaseName());
        m_projectManager->openProject(file);
        persistRecentProjects();
        refreshRecentProjectsMenu();
    }
    updateWindowTitle();
}

void ImageIntegrationProcess::onExportProject()
{
    static const QString contextKey = QStringLiteral("__IMAGE_INTEGRATION_PROJECT__");
    const QString initialDir = rememberedExistingPath(contextKey);

    QString file = an::ui::dialogs::getSaveFileName(process_utils::currentGlobalSettings(), this, tr("Export Project"),
                                                    initialDir, tr("JSON Files (*.json)"));
    if (file.isEmpty())
        return;

    rememberDirectoryForPath(contextKey, file);

    QString errorMessage;
    bool saved = false;
    const QJsonObject json = m_model->toJson();
    if (m_projectManager)
    {
        m_projectManager->setProjectName(QFileInfo(file).completeBaseName());
        saved = m_projectManager->saveAs(json, file, &errorMessage);
    }
    else
    {
        QSaveFile f(file);
        if (f.open(QIODevice::WriteOnly))
        {
            const QByteArray payload = QJsonDocument(json).toJson();
            saved = (f.write(payload) == payload.size() && f.commit());
            if (!saved)
            {
                errorMessage = f.errorString();
                if (errorMessage.isEmpty())
                    errorMessage = tr("Could not save project file.");
                f.cancelWriting();
            }
        }
        else
        {
            errorMessage = tr("Could not open file for writing.");
        }
    }

    if (!saved)
    {
        openNonBlockingMessageBox(this, QMessageBox::Warning, tr("Export Failed"),
                                  errorMessage.isEmpty() ? tr("Could not open file for writing.") : errorMessage);
        return;
    }

    if (m_projectManager)
    {
        persistRecentProjects();
        refreshRecentProjectsMenu();
    }

    m_model->markSaved();
    updateWindowTitle();
}

namespace
{
QString joinGroupKeys(const std::vector<GroupKey>& keys)
{
    QStringList list;
    list.reserve(static_cast<qsizetype>(keys.size()));
    for (const auto& key : keys)
    {
        list.append(key.toCompactString());
    }
    return list.isEmpty() ? QStringLiteral("-") : list.join(QStringLiteral(", "));
}

QString joinIntegrationGroupKeys(const std::vector<IntegrationGroupKey>& keys)
{
    QStringList list;
    list.reserve(static_cast<qsizetype>(keys.size()));
    for (const auto& key : keys)
    {
        list.append(key.toCompactString());
    }
    return list.isEmpty() ? QStringLiteral("-") : list.join(QStringLiteral(", "));
}

void logIncrementalPlan(const ExecutionPlan& plan)
{
    LOG_INFO("ImageIntegrationProcess: incremental plan summary: {}", plan.toSummaryText().toStdString());
    LOG_INFO("ImageIntegrationProcess: incremental plan groups: bias=[{}], dark=[{}], flat=[{}], registration=[{}], "
             "integration=[{}]",
             joinGroupKeys(plan.biasGroupsToProcess).toStdString(),
             joinGroupKeys(plan.darkGroupsToProcess).toStdString(),
             joinGroupKeys(plan.flatGroupsToProcess).toStdString(),
             joinGroupKeys(plan.registrationGroupsToProcess).toStdString(),
             joinIntegrationGroupKeys(plan.integrationGroupsToProcess).toStdString());
}

void setDecisionItem(QTableWidget* table, int row, int column, const QString& text)
{
    auto* item = new QTableWidgetItem(text);
    item->setToolTip(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    table->setItem(row, column, item);
}

void tintDecisionAction(QTableWidgetItem* item, ExecutionPlanDecision::Action action)
{
    if (!item)
    {
        return;
    }

    const auto& colors = an::ui::tokens().colors;
    switch (action)
    {
    case ExecutionPlanDecision::Action::Recompute:
        item->setForeground(QBrush(colors.statusWarning));
        break;
    case ExecutionPlanDecision::Action::Reuse:
        item->setForeground(QBrush(colors.statusSuccess));
        break;
    case ExecutionPlanDecision::Action::Skip:
        item->setForeground(QBrush(colors.textTertiary));
        break;
    }
}

void openExecutionPlanDialog(QWidget* parent, const QString& summary,
                             const std::vector<ExecutionPlanDecision>& decisions, std::function<void()> onAccepted)
{
    auto* dialog = new QDialog(parent);
    dialog->setObjectName(QStringLiteral("executionPlanDialog"));
    dialog->setWindowTitle(QCoreApplication::translate("ImageIntegrationProcess", "Execution Plan"));
    dialog->setWindowModality(parent ? Qt::WindowModal : Qt::ApplicationModal);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setSizeGripEnabled(true);
    dialog->setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    const auto& t = an::ui::tokens();
    dialog->setMinimumSize(t.spacing.xl * 26, t.spacing.xl * 16);

    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(t.spacing.lg, t.spacing.lg, t.spacing.lg, t.spacing.lg);
    layout->setSpacing(t.spacing.md);

    auto* title = new QLabel(QCoreApplication::translate("ImageIntegrationProcess", "Execution Plan"), dialog);
    title->setObjectName(QStringLiteral("executionPlanTitle"));
    title->setFont(t.typography.heading2);
    layout->addWidget(title);

    auto* summaryLabel = new QLabel(summary, dialog);
    summaryLabel->setObjectName(QStringLiteral("executionPlanSummary"));
    summaryLabel->setWordWrap(true);
    layout->addWidget(summaryLabel);

    auto* table = new QTableWidget(static_cast<int>(decisions.size()), 7, dialog);
    table->setObjectName(QStringLiteral("executionPlanTable"));
    table->setHorizontalHeaderLabels({QCoreApplication::translate("ImageIntegrationProcess", "Stage"),
                                      QCoreApplication::translate("ImageIntegrationProcess", "Group"),
                                      QCoreApplication::translate("ImageIntegrationProcess", "Decision"),
                                      QCoreApplication::translate("ImageIntegrationProcess", "Reason"),
                                      QCoreApplication::translate("ImageIntegrationProcess", "Dependencies"),
                                      QCoreApplication::translate("ImageIntegrationProcess", "ETA"),
                                      QCoreApplication::translate("ImageIntegrationProcess", "Outputs")});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->setWordWrap(false);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);

    for (int row = 0; row < static_cast<int>(decisions.size()); ++row)
    {
        const auto& decision = decisions[static_cast<size_t>(row)];
        setDecisionItem(table, row, 0, decision.stage);
        setDecisionItem(table, row, 1, decision.groupKey);
        setDecisionItem(table, row, 2, decision.actionLabel);
        tintDecisionAction(table->item(row, 2), decision.action);
        setDecisionItem(table, row, 3, decision.reason);
        setDecisionItem(table, row, 4, decision.dependencies);
        setDecisionItem(table, row, 5, decision.estimatedTime);
        setDecisionItem(table, row, 6, decision.outputs);
    }
    layout->addWidget(table, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    buttons->setObjectName(QStringLiteral("executionPlanButtons"));
    if (auto* runButton = buttons->button(QDialogButtonBox::Ok))
    {
        runButton->setText(QCoreApplication::translate("ImageIntegrationProcess", "Run"));
        runButton->setProperty("anButtonVariant", QStringLiteral("primary"));
    }
    layout->addWidget(buttons);

    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(dialog, &QDialog::finished, dialog, [dialog, onAccepted = std::move(onAccepted)](int result) {
        if (result == QDialog::Accepted && onAccepted)
        {
            onAccepted();
        }
        dialog->deleteLater();
    });
    dialog->open();
}

using PreparedExecutionPlan = ImageIntegration::PreparedWorkflowExecution;

class ExecutionPlanPreparationDialog final : public QDialog
{
public:
    explicit ExecutionPlanPreparationDialog(QWidget* parent) : QDialog(parent)
    {
        setObjectName(QStringLiteral("executionPlanPreparationDialog"));
        setWindowTitle(QCoreApplication::translate("ImageIntegrationProcess", "Preparing Execution Plan"));
        setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
        setWindowModality(parent ? Qt::WindowModal : Qt::ApplicationModal);

        const auto& t = an::ui::tokens();
        setMinimumSize(t.spacing.xl * 20, t.spacing.xl * 7);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(t.spacing.xl, t.spacing.xl, t.spacing.xl, t.spacing.xl);
        layout->setSpacing(t.spacing.lg);

        auto* description =
            new QLabel(QCoreApplication::translate("ImageIntegrationProcess",
                                                   "Reviewing workflow artifacts and validating the work to run."),
                       this);
        description->setObjectName(QStringLiteral("executionPlanPreparationDescription"));
        description->setProperty("anStatusTone", QStringLiteral("secondary"));
        description->setWordWrap(true);
        description->setFont(t.typography.body);
        description->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        layout->addWidget(description);

        m_progress_card = new an::ui::AnProgressCard(
            QCoreApplication::translate("ImageIntegrationProcess", "Preparing execution plan"), this);
        m_progress_card->setObjectName(QStringLiteral("executionPlanPreparationProgressCard"));
        m_progress_card->setProgressRange(0, 100);
        m_progress_card->setProgressValue(0);
        m_progress_card->setEtaText(
            QCoreApplication::translate("ImageIntegrationProcess", "Checking reusable workflow artifacts…"));
        m_progress_card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        if (auto* progress_bar = m_progress_card->progressBar())
        {
            progress_bar->setObjectName(QStringLiteral("executionPlanPreparationProgressBar"));
            progress_bar->setMinimumHeight(t.spacing.xl);
        }
        layout->addWidget(m_progress_card);
    }

    void setProgressRange(const int minimum, const int maximum)
    {
        m_progress_card->setProgressRange(minimum, maximum);
    }

    void setProgressValue(const int value)
    {
        m_progress_card->setProgressValue(value);
    }

    void setProgressText(const QString& text)
    {
        m_progress_card->setEtaText(text);
    }

private:
    an::ui::AnProgressCard* m_progress_card = nullptr;
};

[[nodiscard]] qint64 elapsedMilliseconds(const std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
}

void prepareExecutionPlanAsync(QWidget* parent, std::function<WorkflowManifest()> prepare_manifest,
                               std::optional<IntegrationGroupKey> forced_integration_group,
                               std::function<void(PreparedExecutionPlan)> on_finished)
{
    auto* progress = new ExecutionPlanPreparationDialog(parent);
    progress->setProperty("anPlanPreparationDismissed", false);
    progress->show();

    auto* watcher = new QFutureWatcher<PreparedExecutionPlan>(progress);
    QObject::connect(progress, &QDialog::rejected, watcher, [progress, watcher]() {
        progress->setProperty("anPlanPreparationDismissed", true);
        watcher->cancel();
    });
    QObject::connect(watcher, &QFutureWatcher<PreparedExecutionPlan>::progressRangeChanged, progress,
                     &ExecutionPlanPreparationDialog::setProgressRange);
    QObject::connect(watcher, &QFutureWatcher<PreparedExecutionPlan>::progressValueChanged, progress,
                     &ExecutionPlanPreparationDialog::setProgressValue);
    QObject::connect(watcher, &QFutureWatcher<PreparedExecutionPlan>::progressTextChanged, progress,
                     &ExecutionPlanPreparationDialog::setProgressText);
    QObject::connect(watcher, &QFutureWatcher<PreparedExecutionPlan>::finished, progress,
                     [watcher, progress, on_finished = std::move(on_finished)]() mutable {
                         const bool has_result = watcher->future().resultCount() > 0;
                         PreparedExecutionPlan prepared;
                         if (has_result)
                             prepared = watcher->result();
                         const bool was_dismissed = progress->property("anPlanPreparationDismissed").toBool();
                         progress->close();
                         watcher->deleteLater();
                         progress->deleteLater();
                         if (!was_dismissed && has_result && on_finished)
                         {
                             on_finished(std::move(prepared));
                         }
                     });

    watcher->setFuture(QtConcurrent::run(
        [prepare_manifest = std::move(prepare_manifest), forced_integration_group = std::move(forced_integration_group),
         session =
             ImageIntegration::detail::applicationEngineSession()](QPromise<PreparedExecutionPlan>& promise) mutable {
            promise.setProgressRange(0, 100);
            PreparedExecutionPlan prepared = ImageIntegration::preparePlannedWorkflowExecution(
                prepare_manifest, forced_integration_group, [&promise]() { return promise.isCanceled(); },
                [&promise](const int percent, const QString& text) { promise.setProgressValueAndText(percent, text); },
                session != nullptr ? ImageIntegration::sessionFrameHeaderReader(*session)
                                   : ImageIntegration::FrameHeaderReader{});
            promise.addResult(std::move(prepared));
        }));
}

void prepareRegistrationSnapshotAsync(
    QWidget* parent, WorkflowManifest manifest,
    std::function<void(std::shared_ptr<const ExecutionPreparationSnapshot>)> on_finished,
    std::function<void()> on_aborted = {})
{
    auto* progress = new ExecutionPlanPreparationDialog(parent);
    progress->setProperty("anPlanPreparationDismissed", false);
    progress->show();

    using Snapshot = std::shared_ptr<const ExecutionPreparationSnapshot>;
    auto* watcher = new QFutureWatcher<Snapshot>(progress);
    QObject::connect(progress, &QDialog::rejected, watcher, [progress, watcher]() {
        progress->setProperty("anPlanPreparationDismissed", true);
        watcher->cancel();
    });
    QObject::connect(
        watcher, &QFutureWatcher<Snapshot>::finished, progress,
        [watcher, progress, on_finished = std::move(on_finished), on_aborted = std::move(on_aborted)]() mutable {
            const Snapshot snapshot = watcher->future().resultCount() > 0 ? watcher->result() : Snapshot{};
            const bool dismissed = progress->property("anPlanPreparationDismissed").toBool();
            progress->close();
            watcher->deleteLater();
            progress->deleteLater();
            if (!dismissed && snapshot && on_finished)
                on_finished(snapshot);
            else if (on_aborted)
                on_aborted();
        });
    watcher->setFuture(QtConcurrent::run([manifest = std::move(manifest)](QPromise<Snapshot>& promise) {
        promise.setProgressRange(0, 100);
        promise.setProgressValueAndText(
            10, QCoreApplication::translate("ImageIntegrationProcess", "Checking registration artifacts…"));
        try
        {
            const auto cancelled = [&promise]() { return promise.isCanceled(); };
            Snapshot snapshot = ExecutionPreparationSnapshot::build(manifest, std::nullopt, cancelled);
            promise.setProgressValueAndText(
                100, QCoreApplication::translate("ImageIntegrationProcess", "Registration plan ready."));
            promise.addResult(std::move(snapshot));
        }
        catch (...)
        {
            promise.addResult({});
        }
    }));
}

struct IntegratedOutputOpenPlan
{
    QSet<QString> skipIntegrationGroupKeys;
    QStringList extraOutputFiles;
};

[[nodiscard]] IntegratedOutputOpenPlan buildIntegratedOutputOpenPlan(const WorkflowManifest& manifest,
                                                                     const WorkflowExecutor::Result& result)
{
    IntegratedOutputOpenPlan plan;
    if (!manifest.combineRgbImages)
    {
        return plan;
    }

    std::vector<ImageIntegration::RgbIntegrationGroupCandidate> candidates;
    candidates.reserve(manifest.integrationGroups.size());
    for (const auto& group : manifest.integrationGroups)
    {
        candidates.push_back(ImageIntegration::RgbIntegrationGroupCandidate{
            group.groupKey.toString(), group.registrationGroupKey.toString(), group.filter, group.outputFilename,
            static_cast<int>(group.lightFiles.size())});
    }

    const auto rgb_groups = ImageIntegration::findEligibleRgbIntegrationGroups(candidates);
    if (rgb_groups.empty())
    {
        return plan;
    }

    QHash<QString, QString> successful_output_by_group_key;
    successful_output_by_group_key.reserve(static_cast<int>(result.integrationGroupResults.size()));
    for (const auto& group_result : result.integrationGroupResults)
    {
        if (group_result.success && !group_result.outputPath.trimmed().isEmpty())
        {
            successful_output_by_group_key.insert(group_result.groupKey, group_result.outputPath);
        }
    }

    for (const auto& rgb_group : rgb_groups)
    {
        const QString combined_key = ImageIntegration::combinedRgbGroupKey(rgb_group.registrationGroupKey);
        const QString combined_output = successful_output_by_group_key.value(combined_key);
        if (combined_output.trimmed().isEmpty())
        {
            continue;
        }

        plan.skipIntegrationGroupKeys.insert(rgb_group.red.groupKey);
        plan.skipIntegrationGroupKeys.insert(rgb_group.green.groupKey);
        plan.skipIntegrationGroupKeys.insert(rgb_group.blue.groupKey);
        plan.extraOutputFiles.append(combined_output);
    }

    plan.extraOutputFiles.removeDuplicates();
    return plan;
}
} // namespace

void ImageIntegrationProcess::applyExecutionOptions(WorkflowManifest& manifest) const
{
    manifest.maxParallelCores = m_stackingTab ? m_stackingTab->maxParallelCores() : 0;
    manifest.combineRgbImages = m_stackingTab ? m_stackingTab->isCombineRgbImagesEnabled() : false;
}

void ImageIntegrationProcess::onExecute()
{
    if (!m_model->isReadyForIntegration())
    {
        openNonBlockingMessageBox(
            this, QMessageBox::Warning, tr("Execution Error"),
            tr("The workflow is not ready for integration. Please check the Summary tab for errors."));
        return;
    }

    const QJsonObject model_snapshot = m_model->toJson();
    const bool reprocess_existing = m_stackingTab ? m_stackingTab->isReprocessExistingEnabled() : false;
    const int max_parallel_cores = m_stackingTab ? m_stackingTab->maxParallelCores() : 0;
    const bool combine_rgb_images = m_stackingTab ? m_stackingTab->isCombineRgbImagesEnabled() : false;
    const QPointer<ImageIntegrationProcess> self(this);
    const bool openIntegratedFiles = m_stackingTab ? m_stackingTab->isOpenIntegratedFilesEnabled() : true;
    prepareExecutionPlanAsync(
        this,
        [model_snapshot, reprocess_existing, max_parallel_cores, combine_rgb_images]() {
            IntegrationDataModel prepared_model;
            prepared_model.fromJson(model_snapshot);
            return ImageIntegration::buildFullWorkflowManifest(
                prepared_model, {reprocess_existing, max_parallel_cores, combine_rgb_images});
        },
        std::nullopt,
        [self, openIntegratedFiles](PreparedExecutionPlan prepared) mutable {
            if (!self || !self->m_model)
            {
                return;
            }

            const ImageIntegration::PreparedWorkflowDisposition disposition =
                ImageIntegration::plannedDisposition(prepared);
            if (disposition == ImageIntegration::PreparedWorkflowDisposition::Failed)
            {
                openNonBlockingMessageBox(self, QMessageBox::Warning, self->tr("Execution Error"),
                                          prepared.errorMessage.trimmed().isEmpty()
                                              ? self->tr("Workflow preparation failed.")
                                              : prepared.errorMessage);
                return;
            }

            if (!checkOutputDirectoryBeforeRun(self, prepared.manifest.outputDirectory))
            {
                return;
            }

            if (disposition == ImageIntegration::PreparedWorkflowDisposition::NothingToProcess)
            {
                openNonBlockingMessageBox(self, QMessageBox::Information, self->tr("Nothing to Process"),
                                          self->tr("No changes detected since the last successful run."));
                return;
            }

            if (prepared.plan.mode == ExecutionMode::Incremental)
            {
                LOG_INFO("ImageIntegrationProcess: starting incremental execution...");
                logIncrementalPlan(prepared.plan);
            }

            if (disposition == ImageIntegration::PreparedWorkflowDisposition::Invalid)
            {
                QStringList lines;
                for (const auto& issue : prepared.validation.issues)
                {
                    if (issue.severity == ValidationIssue::Severity::Error)
                    {
                        lines.append(QStringLiteral("• %1").arg(issue.message));
                    }
                }

                openNonBlockingMessageBox(self, QMessageBox::Warning, self->tr("Execution Error"),
                                          lines.isEmpty() ? self->tr("Invalid workflow configuration.")
                                                          : lines.join(QStringLiteral("\n")));
                return;
            }

            if (disposition == ImageIntegration::PreparedWorkflowDisposition::NoGroups)
            {
                openNonBlockingMessageBox(self, QMessageBox::Information, self->tr("Nothing to Process"),
                                          self->tr("No groups require processing. All outputs may already exist."));
                return;
            }

            const QString summary = prepared.plan.toSummaryText();
            auto decisions = std::move(prepared.decisions);
            WorkflowManifest execution_manifest = std::move(prepared.manifest);
            auto preparation_snapshot = std::move(prepared.preparationSnapshot);
            const WorkflowPreparationTiming preparation_timing = prepared.preparationTiming;
            openExecutionPlanDialog(
                self, summary, decisions,
                [self, manifest = std::move(execution_manifest), openIntegratedFiles, preparation_timing,
                 preparation_snapshot = std::move(preparation_snapshot)]() mutable {
                    if (!self || !self->m_model)
                    {
                        return;
                    }

                    WorkflowExecutionController executionController(self->m_model.get(), self);
                    (void)executionController.runAsyncPrevalidated(
                        manifest,
                        [self, manifest, openIntegratedFiles](const WorkflowExecutor::Result& result) {
                            if (!self || !result.success)
                            {
                                return;
                            }

                            if (self->m_summaryTab && self->m_model)
                            {
                                const int lightCount = static_cast<int>(self->m_model->lightFrames().size());
                                self->m_summaryTab->setLastSuccessfulRun(QDateTime::currentDateTimeUtc(), lightCount,
                                                                         self->m_model->outputDirectory());
                            }

                            if (self->m_model)
                            {
                                const QDir outputDir(self->m_model->outputDirectory());
                                const IntegratedOutputOpenPlan openPlan =
                                    buildIntegratedOutputOpenPlan(manifest, result);
                                ImageIntegration::openIntegratedOutputsForManifest(
                                    outputDir, manifest.integrationGroups, openIntegratedFiles,
                                    openPlan.skipIntegrationGroupKeys, openPlan.extraOutputFiles);
                            }
                        },
                        preparation_timing, std::move(preparation_snapshot));
                });
        });
}

void ImageIntegrationProcess::onRunIntegrationForGroup(const QString& integrationGroupKey)
{
    const auto keyOpt = GroupKey::fromString(integrationGroupKey);
    if (!keyOpt.has_value())
    {
        return;
    }

    const QJsonObject model_snapshot = m_model->toJson();
    const bool reprocess_existing = m_stackingTab ? m_stackingTab->isReprocessExistingEnabled() : false;
    const int max_parallel_cores = m_stackingTab ? m_stackingTab->maxParallelCores() : 0;
    const bool combine_rgb_images = m_stackingTab ? m_stackingTab->isCombineRgbImagesEnabled() : false;
    const IntegrationGroupKey requested_group{*keyOpt};
    const QPointer<ImageIntegrationProcess> self(this);
    const bool openIntegratedFiles = m_stackingTab ? m_stackingTab->isOpenIntegratedFilesEnabled() : true;
    prepareExecutionPlanAsync(
        this,
        [model_snapshot, reprocess_existing, max_parallel_cores, combine_rgb_images, requested_group]() {
            IntegrationDataModel prepared_model;
            prepared_model.fromJson(model_snapshot);

            ManifestBuilder builder(prepared_model);
            builder.setReprocessExisting(reprocess_existing);
            builder.setIncludeBiasFrames(true);
            builder.setIncludeDarkFrames(true);
            builder.setIncludeFlatFrames(true);
            builder.setIncludeLightFrames(true);

            WorkflowManifest manifest = builder.buildForIntegrationGroup(requested_group);
            manifest.maxParallelCores = max_parallel_cores;
            manifest.combineRgbImages = combine_rgb_images;
            for (auto& group : manifest.integrationGroups)
            {
                group.skipIfExists = false;
            }
            return manifest;
        },
        requested_group,
        [self, openIntegratedFiles](PreparedExecutionPlan prepared) mutable {
            if (!self || !self->m_model)
            {
                return;
            }

            if (!prepared.success)
            {
                openNonBlockingMessageBox(self, QMessageBox::Warning, self->tr("Execution Error"),
                                          prepared.errorMessage.trimmed().isEmpty()
                                              ? self->tr("Workflow preparation failed.")
                                              : prepared.errorMessage);
                return;
            }

            if (prepared.manifest.integrationGroups.empty())
            {
                openNonBlockingMessageBox(self, QMessageBox::Warning, self->tr("Execution Error"),
                                          self->tr("No valid frames found for the selected integration group."));
                return;
            }

            if (!checkOutputDirectoryBeforeRun(self, prepared.manifest.outputDirectory))
            {
                return;
            }
            if (prepared.plan.mode == ExecutionMode::Incremental)
            {
                logIncrementalPlan(prepared.plan);
            }

            if (prepared.validation.hasErrors())
            {
                QStringList lines;
                for (const auto& issue : prepared.validation.issues)
                {
                    if (issue.severity == ValidationIssue::Severity::Error)
                    {
                        lines.append(QStringLiteral("• %1").arg(issue.message));
                    }
                }
                openNonBlockingMessageBox(self, QMessageBox::Warning, self->tr("Execution Error"),
                                          lines.isEmpty() ? self->tr("Invalid workflow configuration.")
                                                          : lines.join(QStringLiteral("\n")));
                return;
            }

            const QString summary = prepared.plan.toSummaryText();
            auto decisions = std::move(prepared.decisions);
            WorkflowManifest execution_manifest = std::move(prepared.manifest);
            auto preparation_snapshot = std::move(prepared.preparationSnapshot);
            const WorkflowPreparationTiming preparation_timing = prepared.preparationTiming;
            openExecutionPlanDialog(
                self, summary, decisions,
                [self, manifest = std::move(execution_manifest), openIntegratedFiles, preparation_timing,
                 preparation_snapshot = std::move(preparation_snapshot)]() mutable {
                    if (!self || !self->m_model)
                    {
                        return;
                    }

                    WorkflowExecutionController executionController(self->m_model.get(), self);
                    (void)executionController.runAsyncPrevalidated(
                        manifest,
                        [self, manifest, openIntegratedFiles](const WorkflowExecutor::Result& result) {
                            if (!self)
                            {
                                return;
                            }

                            if (result.success && self->m_summaryTab && self->m_model)
                            {
                                const int lightCount = static_cast<int>(self->m_model->lightFrames().size());
                                self->m_summaryTab->setLastSuccessfulRun(QDateTime::currentDateTimeUtc(), lightCount,
                                                                         self->m_model->outputDirectory());
                            }

                            if (result.success && self->m_model)
                            {
                                const QDir outputDir(self->m_model->outputDirectory());
                                const IntegratedOutputOpenPlan openPlan =
                                    buildIntegratedOutputOpenPlan(manifest, result);
                                ImageIntegration::openIntegratedOutputsForManifest(
                                    outputDir, manifest.integrationGroups, openIntegratedFiles,
                                    openPlan.skipIntegrationGroupKeys, openPlan.extraOutputFiles);
                            }

                            // After single group run, refresh registration metrics if registration happened.
                            if (self->m_registrationTab)
                            {
                                self->m_registrationTab->refreshAll();
                            }
                        },
                        preparation_timing, std::move(preparation_snapshot));
                });
        });
}

namespace
{
[[maybe_unused]] [[nodiscard]] QString getFilterNameForFrame(const IntegrationDataModel& model, const FrameId& id)
{
    auto frame_opt = model.frames(FrameType::Light).findById(id);
    if (!frame_opt.has_value())
    {
        return QStringLiteral("(unknown)");
    }
    const QString f = frame_opt->metadata.filter().trimmed();
    return f.isEmpty() ? QStringLiteral("(unknown)") : f;
}

[[nodiscard]] QStringList
applyAutoReferenceFromMetricsDb(IntegrationDataModel& model, const QString& metrics_path,
                                const std::vector<WorkflowManifest::RegistrationGroup>& groups)
{
    QStringList changed_groups;
    changed_groups.reserve(groups.size());

    for (const auto& group : groups)
    {
        if (!group.settings.autoReferenceDuringRegistration)
        {
            continue;
        }

        const RegistrationMetricsRepository repository(metrics_path);
        QString pick_error;
        const auto pick = repository.pickBestReference(
            group.groupKey.toString(), group.settings.version, group.lightFiles,
            [&model](const FrameId& id) -> QString { return getFilterNameForFrame(model, id); }, pick_error);
        if (!pick.has_value())
        {
            LOG_WARNING("ImageIntegrationProcess: auto-reference during registration failed (group='{}', reason='{}')",
                        group.groupKey.toCompactString().toStdString(), pick_error.toStdString());
            continue;
        }

        const QString current_ref =
            group.referenceFrame.has_value() ? group.referenceFrame->canonicalPath() : QString();
        if (current_ref == pick->id.canonicalPath())
        {
            continue;
        }

        auto frame_opt = model.frames(FrameType::Light).findById(pick->id);
        if (!frame_opt.has_value())
        {
            LOG_WARNING(
                "ImageIntegrationProcess: auto-reference selected frame not found in model (group='{}', frame='{}')",
                group.groupKey.toCompactString().toStdString(), pick->id.canonicalPath().toStdString());
            continue;
        }

        const RegistrationGroupKey reg_key = RegistrationGroupKey::fromFrame(*frame_opt);
        LOG_INFO(
            "ImageIntegrationProcess: auto-reference selected (group='{}', frame='{}', succeeded={}, score={:.4f})",
            group.groupKey.toCompactString().toStdString(), pick->id.canonicalPath().toStdString(),
            pick->metrics.succeeded, pick->score);
        model.setRegistrationReferenceFrame(reg_key, pick->id);

        const auto it_after = model.registrationSettings().find(group.groupKey);
        const QString updated_ref =
            (it_after != model.registrationSettings().end()) ? it_after->second.referenceImage.trimmed() : QString();
        if (updated_ref == pick->id.canonicalPath())
        {
            changed_groups.push_back(group.groupKey.toString());
        }
        else
        {
            LOG_WARNING("ImageIntegrationProcess: auto-reference selection rejected by model (group='{}', "
                        "requested='{}', actual='{}')",
                        group.groupKey.toCompactString().toStdString(), pick->id.canonicalPath().toStdString(),
                        updated_ref.toStdString());
        }
    }

    return changed_groups;
}
} // namespace

void ImageIntegrationProcess::finishRegistrationOnlyRun(const bool success)
{
    if (success && m_summaryTab && m_model)
    {
        const int lightCount = static_cast<int>(m_model->lightFrames().size());
        m_summaryTab->setLastSuccessfulRun(QDateTime::currentDateTimeUtc(), lightCount, m_model->outputDirectory());
    }

    if (m_registrationTab)
    {
        m_registrationTab->refreshAll();
    }
}

void ImageIntegrationProcess::onRunRegistrationForGroup(const QString& registrationGroupKey)
{
    if (!m_model)
    {
        return;
    }
    WorkflowManifest pass1;
    const bool reprocess_existing = m_stackingTab ? m_stackingTab->isReprocessExistingEnabled() : false;
    WorkflowExecutionController manifestBuilder(m_model.get(), this);
    if (!manifestBuilder.buildRegistrationOnlyManifestForGroup(registrationGroupKey, reprocess_existing, pass1))
    {
        return;
    }
    applyExecutionOptions(pass1);

    const QPointer<ImageIntegrationProcess> self(this);
    prepareRegistrationSnapshotAsync(this, pass1, [self, pass1](const auto& preparation_snapshot) {
        if (!self)
            return;
        if (!checkOutputDirectoryBeforeRun(self, pass1.outputDirectory))
            return;
        ExecutionPlan plan;
        plan.mode = ExecutionMode::RegistrationOnly;
        const auto decisions = buildExecutionPlanDecisions(pass1, plan, *preparation_snapshot);
        openExecutionPlanDialog(self, plan.toSummaryText(), decisions, [self, pass1, preparation_snapshot]() {
            if (!self || !self->m_model)
                return;

            WorkflowExecutionController executionController(self->m_model.get(), self);
            (void)executionController.runAsyncValidated(
                pass1,
                [self, pass1](const WorkflowExecutor::Result& result) {
                    if (!self)
                        return;
                    if (result.success && self->m_model)
                    {
                        const QString metrics_path =
                            QDir(pass1.outputDirectory).filePath(QStringLiteral("registration_metrics.db"));
                        (void)applyAutoReferenceFromMetricsDb(*self->m_model, metrics_path, pass1.registrationGroups);
                    }
                    self->finishRegistrationOnlyRun(result.success);
                },
                {}, preparation_snapshot);
        });
    });
}

void ImageIntegrationProcess::onRunRegistrationForAllGroups()
{
    if (!m_model)
    {
        return;
    }

    if (m_runRegistrationAllInProgress || property("anRegistrationPreparationInProgress").toBool())
    {
        return;
    }

    const bool reprocess_existing = m_stackingTab ? m_stackingTab->isReprocessExistingEnabled() : false;
    WorkflowExecutionController manifestBuilder(m_model.get(), this);
    WorkflowManifest manifest = manifestBuilder.buildRegistrationOnlyManifestAll(reprocess_existing);
    applyExecutionOptions(manifest);
    const WorkflowManifest unfilteredManifest = manifest;
    if (!manifest.reprocessExisting && manifest.outputDirectory.trimmed().isEmpty())
    {
        openNonBlockingMessageBox(this, QMessageBox::Information, tr("Run Registration"),
                                  tr("Set an output directory before running registration."));
        return;
    }

    const QPointer<ImageIntegrationProcess> self(this);
    setProperty("anRegistrationPreparationInProgress", true);
    prepareRegistrationSnapshotAsync(
        this, manifest,
        [self, manifest, unfilteredManifest](const auto& preparation_snapshot) mutable {
            if (!self)
                return;
            self->setProperty("anRegistrationPreparationInProgress", false);
            if (!self->m_model)
                return;
            if (!checkOutputDirectoryBeforeRun(self, unfilteredManifest.outputDirectory))
                return;
            if (!manifest.reprocessExisting && preparation_snapshot->registrationDatabaseUsable())
            {
                std::vector<WorkflowManifest::RegistrationGroup> to_process;
                to_process.reserve(manifest.registrationGroups.size());
                const auto analyses = analyzeRegistrationGroupsWork(*preparation_snapshot, manifest);
                for (const auto& entry : analyses)
                {
                    if (!entry.analysis.needsWork)
                        continue;
                    QStringList reasons;
                    for (const auto& reason : entry.analysis.reasons)
                        reasons << describeRegistrationWorkReason(reason);
                    LOG_INFO("ImageIntegrationProcess: registration-all group queued (group='{}', reason='{}')",
                             entry.group.groupKey.toString().toStdString(),
                             reasons.join(QStringLiteral("; ")).toStdString());
                    to_process.push_back(entry.group);
                }
                manifest.registrationGroups = std::move(to_process);
            }

            if (manifest.registrationGroups.empty())
            {
                const QString metrics_path =
                    QDir(unfilteredManifest.outputDirectory).filePath(QStringLiteral("registration_metrics.db"));
                if (QFileInfo::exists(metrics_path))
                {
                    const QStringList changed_groups = applyAutoReferenceFromMetricsDb(
                        *self->m_model, metrics_path, unfilteredManifest.registrationGroups);
                    if (!changed_groups.isEmpty() && self->m_registrationTab)
                        self->m_registrationTab->refreshAll();
                }
                openNonBlockingMessageBox(self, QMessageBox::Information, self->tr("Run Registration"),
                                          self->tr("No registration updates were detected for any group."));
                return;
            }

            ExecutionPlan plan;
            plan.mode = ExecutionMode::RegistrationOnly;
            const auto decisions = buildExecutionPlanDecisions(manifest, plan, *preparation_snapshot);
            openExecutionPlanDialog(
                self, plan.toSummaryText(), decisions, [self, manifest, unfilteredManifest, preparation_snapshot]() {
                    if (!self || !self->m_model)
                        return;
                    self->m_runRegistrationAllInProgress = true;
                    WorkflowExecutionController executionController(self->m_model.get(), self);
                    const bool launched = executionController.runAsyncValidated(
                        manifest,
                        [self, unfilteredManifest](const WorkflowExecutor::Result& result) {
                            if (!self)
                                return;
                            if (result.success && self->m_model)
                            {
                                const QString metrics_path = QDir(unfilteredManifest.outputDirectory)
                                                                 .filePath(QStringLiteral("registration_metrics.db"));
                                (void)applyAutoReferenceFromMetricsDb(*self->m_model, metrics_path,
                                                                      unfilteredManifest.registrationGroups);
                            }
                            self->m_runRegistrationAllInProgress = false;
                            self->finishRegistrationOnlyRun(result.success);
                        },
                        {}, preparation_snapshot);
                    if (!launched && self)
                        self->m_runRegistrationAllInProgress = false;
                });
        },
        [self]() {
            if (self)
                self->setProperty("anRegistrationPreparationInProgress", false);
        });
}

void ImageIntegrationProcess::onFixRequested(FixTarget target)
{
    switch (target)
    {
    case FixTarget::OutputDirectory:
        if (outputDirectoryEdit)
        {
            outputDirectoryEdit->setFocus();
            outputDirectoryEdit->selectAll();
        }
        break;

    case FixTarget::BiasFrames:
        setCurrentLogicalIndex(0);
        break;

    case FixTarget::DarkFrames:
        setCurrentLogicalIndex(1);
        break;

    case FixTarget::FlatFrames:
        setCurrentLogicalIndex(2);
        break;

    case FixTarget::LightFrames:
        setCurrentLogicalIndex(3);
        break;

    case FixTarget::CalibrationTab:
        setCurrentLogicalIndex(4);
        break;

    case FixTarget::RegistrationTab:
        setCurrentLogicalIndex(5);
        break;

    case FixTarget::IntegrationTab:
        // FixTarget keeps the historical name; the unified UI labels this tab "Stacking".
        setCurrentLogicalIndex(6);
        break;

    case FixTarget::None:
        break;
    }
}

void ImageIntegrationProcess::onFixWithGroupRequested(FixTarget target, const QString& groupKey)
{
    // First navigate to the appropriate tab
    onFixRequested(target);

    // If no group key provided, nothing more to do
    if (groupKey.isEmpty())
    {
        return;
    }

    // For calibration-related issues, highlight the group in the Calibration tab's light table.
    // The Calibration tab shows which light groups have calibration matches/issues.
    if (target == FixTarget::CalibrationTab && m_calibrationTab)
    {
        // Parse the groupKey back to a GroupKey and select in the calibration table
        const auto keyOpt = GroupKey::fromString(groupKey);
        if (keyOpt.has_value())
        {
            m_calibrationTab->showGroupInTable(FrameType::Light, keyOpt.value());
        }
    }
}

void ImageIntegrationProcess::updateButtonStates()
{
    bool ready = m_model && m_model->isReadyForIntegration();
    if (m_executeAction)
    {
        m_executeAction->setEnabled(ready);
    }

    bool hasSelection = false;
    const int index = currentFrameTabIndex();
    if (index >= 0 && index < kFrameTabCount && m_frameTrees[index])
    {
        hasSelection = !m_frameTrees[index]->selectedFilePaths().isEmpty();
    }
    if (m_clearSelectedAction)
    {
        m_clearSelectedAction->setEnabled(hasSelection);
    }

    bool hasFrames = m_model && (!m_model->biasFrames().empty() || !m_model->darkFrames().empty() ||
                                 !m_model->flatFrames().empty() || !m_model->lightFrames().empty());
    if (m_clearAllAction)
    {
        m_clearAllAction->setEnabled(hasFrames);
    }
}

void ImageIntegrationProcess::changeFrameType(const QStringList& paths, FrameType newType)
{
    if (paths.isEmpty())
        return;

    // Determine the current type of the first frame (all should be same type)
    FrameType currentType = FrameType::Light;
    bool foundCurrentType = false;

    for (int i = 0; i < kFrameTabCount && !foundCurrentType; ++i)
    {
        FrameType type = frameTypeForIndex(i);
        if (m_model->frames(type).findById(FrameId(paths.first())).has_value())
        {
            currentType = type;
            foundCurrentType = true;
        }
    }

    if (!foundCurrentType || currentType == newType)
        return;

    // Check for calibration conflicts if moving FROM a calibration type (Bias/Dark/Flat)
    ImageIntegration::TypeChangeConflictInfo conflicts;

    if (currentType == FrameType::Bias || currentType == FrameType::Dark || currentType == FrameType::Flat)
    {
        CalibrationMatcher matcher = buildConfiguredCalibrationMatcher(m_model.get());

        // Find which groups would be orphaned by this move
        QSet<QString> movingPaths(paths.begin(), paths.end());

        // Helper lambda to get groups for a frame type
        auto getGroupsForType = [this](FrameType ft) -> const GroupedFrameView& {
            switch (ft)
            {
            case FrameType::Bias:
                return m_model->biasGroups();
            case FrameType::Dark:
                return m_model->darkGroups();
            case FrameType::Flat:
                return m_model->flatGroups();
            default:
                return m_model->lightGroups();
            }
        };

        // Check which light groups are using frames from the moving set
        for (const auto& lightGroup : m_model->lightGroups())
        {
            if (lightGroup.frames.empty())
                continue;
            const auto lightKey = LightGroupKey::fromFrame(lightGroup.frames.front());

            CalibrationMatch match;
            if (currentType == FrameType::Bias)
            {
                // A light takes the bias only where no dark is applied to it, unless CCD mode is enabled.
                const bool ccd_mode = m_model->calibrationMatchingPolicy().subtractBiasFromDark;
                if (!lightTakesBias(matcher.findDark(lightKey), ccd_mode))
                {
                    match = {};
                }
                else
                {
                    match = matcher.findBias(lightKey);
                }
            }
            else if (currentType == FrameType::Dark)
            {
                match = matcher.findDark(lightKey);
            }
            else if (currentType == FrameType::Flat)
            {
                match = matcher.findFlat(lightKey);
            }

            // Check if the match points to a group containing any of the moving frames
            if (match.type != CalibrationMatch::MatchType::None && !match.matchedGroupKey.isEmpty())
            {
                // Find the group and check if it contains any moving frames
                const auto& groups = getGroupsForType(currentType);
                for (const auto& group : groups)
                {
                    if (group.key.toString() == match.matchedGroupKey)
                    {
                        for (const auto& frame : group.frames)
                        {
                            if (movingPaths.contains(frame.id.canonicalPath()))
                            {
                                conflicts.affectedLightGroups.append(lightGroup.key.toCompactString());
                                conflicts.hasConflicts = true;
                                break;
                            }
                        }
                        break;
                    }
                }
            }
        }

        // Check which flat groups are using frames from the moving set (only for Bias/Dark)
        if (currentType == FrameType::Bias || currentType == FrameType::Dark)
        {
            for (const auto& flatGroup : m_model->flatGroups())
            {
                if (flatGroup.frames.empty())
                    continue;
                const auto flatAsLight = LightGroupKey::fromFrame(flatGroup.frames.front());

                CalibrationMatch match;
                if (currentType == FrameType::Bias)
                {
                    // A flat takes its dark alone, in CCD mode too, and the bias only where no dark is applied to it.
                    if (!flatTakesBias(matcher.findDark(flatAsLight)))
                    {
                        match = {};
                    }
                    else
                    {
                        match = matcher.findBias(flatAsLight);
                    }
                }
                else
                {
                    match = matcher.findDark(flatAsLight);
                }

                if (match.type != CalibrationMatch::MatchType::None && !match.matchedGroupKey.isEmpty())
                {
                    const auto& groups = getGroupsForType(currentType);
                    for (const auto& group : groups)
                    {
                        if (group.key.toString() == match.matchedGroupKey)
                        {
                            for (const auto& frame : group.frames)
                            {
                                if (movingPaths.contains(frame.id.canonicalPath()))
                                {
                                    conflicts.affectedFlatGroups.append(flatGroup.key.toCompactString());
                                    conflicts.hasConflicts = true;
                                    break;
                                }
                            }
                            break;
                        }
                    }
                }
            }
        }

        // Check for orphaned overrides
        for (const auto& override : m_model->calibrationOverrides())
        {
            if (movingPaths.contains(override.masterPath))
            {
                conflicts.orphanedOverrides.append(override.masterPath);
                conflicts.hasConflicts = true;
            }
        }
    }

    // Show confirmation if there are conflicts
    if (conflicts.hasConflicts)
    {
        const QString fromTypeName = toString(currentType);
        const QString toTypeName = toString(newType);
        const QString message =
            ImageIntegration::buildTypeChangeConflictMessage(paths, fromTypeName, toTypeName, conflicts);

        auto* messageBox = new QMessageBox(QMessageBox::Warning, tr("Calibration Conflicts"), message,
                                           QMessageBox::Yes | QMessageBox::No, this);
        messageBox->setAttribute(Qt::WA_DeleteOnClose);
        messageBox->setDefaultButton(QMessageBox::No);
        const QPointer<ImageIntegrationProcess> self(this);
        QObject::connect(messageBox, &QMessageBox::finished, this, [self, paths, newType](const int result) {
            if (self && result == QMessageBox::Yes)
            {
                self->applyFrameTypeChange(paths, newType);
            }
        });
        messageBox->open();
        return;
    }

    applyFrameTypeChange(paths, newType);
}

void ImageIntegrationProcess::applyFrameTypeChange(const QStringList& paths, const FrameType newType)
{
    if (!m_model)
    {
        return;
    }

    std::vector<FrameTypeChange> changes;
    changes.reserve(paths.size());
    for (const auto& path : paths)
    {
        for (int i = 0; i < kFrameTabCount; ++i)
        {
            FrameType checkType = frameTypeForIndex(i);
            auto frameOpt = m_model->frames(checkType).findById(FrameId(path));
            if (frameOpt.has_value())
            {
                if (checkType != newType)
                {
                    changes.push_back(FrameTypeChange{checkType, frameOpt.value(), newType});
                }
                break;
            }
        }
    }

    if (changes.empty())
    {
        return;
    }

    if (m_undoStack)
    {
        m_undoStack->push(new ChangeFrameTypeCommand(m_model.get(), std::move(changes)));
    }
    else
    {
        m_model->beginBatchUpdate();
        for (const auto& change : changes)
        {
            m_model->removeFrames(change.oldType, {change.oldInfo.id.canonicalPath()});
            m_model->addFrames(change.newType, {remapFrameInfoType(change.oldInfo, change.newType)});
        }
        m_model->endBatchUpdate();
    }
}

void ImageIntegrationProcess::syncSelectionToModel(int treeIndex)
{
    if (treeIndex < 0 || treeIndex >= kFrameTabCount || !m_frameTrees[treeIndex])
        return;

    QStringList paths = m_frameTrees[treeIndex]->selectedFilePaths();
    std::vector<FrameId> ids;
    ids.reserve(paths.size());
    for (const auto& path : paths)
    {
        ids.emplace_back(path);
    }

    m_model->setSelectedFrameIds(ids);

    QList<GroupKey> groups = m_frameTrees[treeIndex]->selectedGroups();
    std::vector<GroupKey> groupKeys(groups.begin(), groups.end());
    m_model->setSelectedGroupKeys(groupKeys);
}

void ImageIntegrationProcess::syncSelectionFromModel()
{
    const auto& selectedIds = m_model->selectedFrameIds();

    for (int i = 0; i < kFrameTabCount; ++i)
    {
        if (!m_frameTrees[i])
            continue;

        auto* selModel = m_frameTrees[i]->selectionModel();
        auto* treeModel = m_frameSourceTabs[i]->treeModel();

        QItemSelection selection;
        for (const auto& id : selectedIds)
        {
            QModelIndex idx = treeModel->findIndexForPath(id.canonicalPath());
            if (idx.isValid())
            {
                selection.select(idx, idx);
            }
        }

        QSignalBlocker blocker(selModel); // Prevent recursive sync
        selModel->select(selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }

    if (selectedIds.empty())
        return;

    // Update preview with first selected frame
    const int index = currentFrameTabIndex();
    FrameSourceTab* tab = frameSourceTabForIndex(index);
    ImageViewControl* preview = tab ? tab->previewPanel() : nullptr;
    if (preview)
    {
        preview->loadImageFromFile(selectedIds.front().canonicalPath().toStdString());
    }
}

void ImageIntegrationProcess::onGoToGroup()
{
    const int tabIndex = currentFrameTabIndex();
    FrameSourceTab* tab = frameSourceTabForIndex(tabIndex);
    if (!tab || !tab->treeModel())
    {
        return;
    }

    const QPointer<ImageIntegrationProcess> self(this);
    openNonBlockingTextInput(this, tr("Go to Group"), tr("Enter group name or filter text:"),
                             [self, tabIndex](const QString& text) {
                                 if (!self || text.trimmed().isEmpty())
                                 {
                                     return;
                                 }

                                 FrameSourceTab* currentTab = self->frameSourceTabForIndex(tabIndex);
                                 if (!currentTab || !currentTab->treeModel() || !currentTab->tree())
                                 {
                                     return;
                                 }

                                 // Search for matching group in tree model
                                 GroupTreeModel* model = currentTab->treeModel();
                                 const int groupCount = model->rowCount(QModelIndex());

                                 for (int g = 0; g < groupCount; ++g)
                                 {
                                     const QModelIndex groupIdx = model->index(g, 0, QModelIndex());
                                     if (!groupIdx.isValid())
                                     {
                                         continue;
                                     }

                                     const QString groupName = groupIdx.data(Qt::DisplayRole).toString();
                                     if (groupName.contains(text, Qt::CaseInsensitive))
                                     {
                                         currentTab->tree()->setCurrentIndex(groupIdx);
                                         currentTab->tree()->scrollTo(groupIdx, QAbstractItemView::PositionAtCenter);
                                         currentTab->tree()->expand(groupIdx);
                                         return;
                                     }
                                 }

                                 // No match found
                                 openNonBlockingMessageBox(self, QMessageBox::Information, self->tr("Go to Group"),
                                                           self->tr("No group matching \"%1\" was found.").arg(text));
                             });
}

void ImageIntegrationProcess::goToGroupDefinition(FrameType type, const GroupKey& key)
{
    const int tabIndex = indexFor(type);
    setCurrentLogicalIndex(tabIndex);

    FrameSourceTab* tab = frameSourceTabForIndex(tabIndex);
    if (!tab || !tab->tree() || !tab->treeModel())
    {
        return;
    }

    // Ensure the group is visible.
    if (auto* search = tab->searchBar())
    {
        const QSignalBlocker blocker(search);
        search->setText(QString());
    }
    tab->treeModel()->clearFilter();
    tab->refreshFilterStats();

    GroupTreeModel* model = tab->treeModel();
    FrameTreeView* tree = tab->tree();

    QModelIndex found;
    std::deque<QModelIndex> pending;
    pending.push_back(QModelIndex{});

    while (!pending.empty() && !found.isValid())
    {
        const QModelIndex parent = pending.front();
        pending.pop_front();

        const int rowCount = model->rowCount(parent);
        for (int row = 0; row < rowCount; ++row)
        {
            const QModelIndex idx = model->index(row, 0, parent);
            if (!idx.isValid())
            {
                continue;
            }

            if (model->isGroup(idx) && model->getGroupKey(idx) == key)
            {
                found = idx;
                break;
            }

            pending.push_back(idx);
        }
    }

    if (!found.isValid())
    {
        openNonBlockingMessageBox(
            this, QMessageBox::Information, tr("Go to Group"),
            tr("The group \"%1\" could not be found in the %2 tree.").arg(key.toCompactString(), toString(type)));
        return;
    }

    for (QModelIndex parent = found.parent(); parent.isValid(); parent = parent.parent())
    {
        tree->expand(parent);
    }
    tree->setCurrentIndex(found);
    tree->scrollTo(found, QAbstractItemView::PositionAtCenter);
    tree->expand(found);
}

void ImageIntegrationProcess::showKeyboardShortcutsHelp()
{
    const QString shortcuts = tr("<h3>Keyboard Shortcuts</h3>"
                                 "<table>"
                                 "<tr><td><b>Ctrl+O</b></td><td>Add Files (Auto-sort by file type)</td></tr>"
                                 "<tr><td><b>Ctrl+Shift+O</b></td><td>Add Folder (Recursive, auto-sort)</td></tr>"
                                 "<tr><td><b>Delete</b></td><td>Remove Selected Frames</td></tr>"
                                 "<tr><td><b>Ctrl+Shift+Delete</b></td><td>Remove All Frames</td></tr>"
                                 "<tr><td><b>Ctrl+I</b></td><td>Import Project</td></tr>"
                                 "<tr><td><b>Ctrl+E</b></td><td>Export Project</td></tr>"
                                 "<tr><td><b>Ctrl+Z</b></td><td>Undo</td></tr>"
                                 "<tr><td><b>Ctrl+Y</b></td><td>Redo</td></tr>"
                                 "<tr><td><b>Ctrl+Enter</b></td><td>Execute Integration</td></tr>"
                                 "<tr><td><b>F5</b></td><td>Refresh View</td></tr>"
                                 "<tr><td><b>Ctrl+F</b></td><td>Focus Search Bar</td></tr>"
                                 "<tr><td><b>Ctrl+G</b></td><td>Go to Group</td></tr>"
                                 "<tr><td><b>Ctrl+A</b></td><td>Select All Frames</td></tr>"
                                 "<tr><td><b>Escape</b></td><td>Clear Selection</td></tr>"
                                 "<tr><td><b>Home</b></td><td>Go to First Frame</td></tr>"
                                 "<tr><td><b>End</b></td><td>Go to Last Frame</td></tr>"
                                 "<tr><td><b>Ctrl+1..8</b></td><td>Switch Tab</td></tr>"
                                 "<tr><td><b>F1</b></td><td>Show This Help</td></tr>"
                                 "</table>");

    openNonBlockingMessageBox(this, QMessageBox::Information, tr("Keyboard Shortcuts"), shortcuts);
}

void ImageIntegrationProcess::setCurrentLogicalIndex(int index)
{
    if (index < 0 || index >= kLogicalTabCount)
    {
        return;
    }

    if (index < kFrameTabCount)
    {
        if (rootTabWidget)
            rootTabWidget->setCurrentIndex(toIndex(RootTabIndex::FrameSources));
        if (sourceTabWidget)
            sourceTabWidget->setCurrentIndex(index);
    }
    else
    {
        if (rootTabWidget)
            rootTabWidget->setCurrentIndex(toIndex(RootTabIndex::Workflow));
        if (workflowTabWidget)
            workflowTabWidget->setCurrentIndex(index - toIndex(LogicalTabIndex::Calibration));
    }
}

int ImageIntegrationProcess::currentLogicalIndex() const
{
    if (!rootTabWidget)
    {
        return 0;
    }

    const int rootIdx = rootTabWidget->currentIndex();
    if (rootIdx == toIndex(RootTabIndex::FrameSources))
    {
        return sourceTabWidget ? sourceTabWidget->currentIndex() : 0;
    }

    return workflowTabWidget ? (toIndex(LogicalTabIndex::Calibration) + workflowTabWidget->currentIndex())
                             : toIndex(LogicalTabIndex::Calibration);
}

void ImageIntegrationProcess::setupTabIcons()
{
    if (sourceTabWidget)
    {
        sourceTabWidget->setTabIcon(0, integrationIcon(QStringLiteral("image_linear"))); // Bias
        sourceTabWidget->setTabIcon(1, integrationIcon(QStringLiteral("image_linear"))); // Darks
        sourceTabWidget->setTabIcon(2, integrationIcon(QStringLiteral("image_linear"))); // Flats
        sourceTabWidget->setTabIcon(3, integrationIcon(QStringLiteral("camera"))); // Lights
    }

    if (workflowTabWidget)
    {
        workflowTabWidget->setTabIcon(0, integrationIcon(QStringLiteral("settings"))); // Calibration
        workflowTabWidget->setTabIcon(1, integrationIcon(QStringLiteral("grid"))); // Registration
        workflowTabWidget->setTabIcon(2, integrationIcon(QStringLiteral("stack"))); // Stacking
        workflowTabWidget->setTabIcon(3, integrationIcon(QStringLiteral("summary"))); // Summary
    }
}
