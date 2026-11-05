// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "WorkflowProgressDialogV2.h"
#include "app/UiStyleTokens.h"
#include "theme/AnDesignTokens.h"
#include "processes/ImageIntegration/widgets/StageTimelineWidget.h"
#include "processes/ImageIntegration/workflow/WorkflowProgressController.h"

#include <QCloseEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QProgressBar>
#include <QLabel>
#include <QTextEdit>
#include <QPushButton>
#include "components/AnCardWidget.h"
#include <QFrame>
#include <QElapsedTimer>
#include <QScrollBar>
#include <QCoreApplication>
#include <QMessageBox>
#include <QTextOption>
#include <QTimer>
#include <QTime>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>

#include "app/DesktopUtils.h"
#include <QtGlobal>
#include <QGuiApplication>

#include <algorithm>

#include <verdigris/wobjectimpl.h>

namespace
{
QString toCssColor(const QColor& color)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alpha());
}

QColor blendColor(const QColor& base, const QColor& overlay, const qreal overlayWeight)
{
    const qreal w = qBound(0.0, overlayWeight, 1.0);
    const qreal inv = 1.0 - w;
    return QColor(qRound(base.red() * inv + overlay.red() * w), qRound(base.green() * inv + overlay.green() * w),
                  qRound(base.blue() * inv + overlay.blue() * w), qRound(base.alpha() * inv + overlay.alpha() * w));
}

QColor workflowLogColor(const QPalette& palette, const WorkflowLogLevel level)
{
    using an::ui::styling::SemanticColorRole;
    switch (level)
    {
    case WorkflowLogLevel::Header:
        return an::ui::styling::semanticColor(palette, SemanticColorRole::AccentPrimary);
    case WorkflowLogLevel::Progress:
    case WorkflowLogLevel::Detail:
        return an::ui::styling::semanticColor(palette, SemanticColorRole::TextSecondary);
    case WorkflowLogLevel::Success:
        return an::ui::styling::semanticColor(palette, SemanticColorRole::StatusSuccess);
    case WorkflowLogLevel::Warning:
        return an::ui::styling::semanticColor(palette, SemanticColorRole::StatusWarning);
    case WorkflowLogLevel::Error:
        return an::ui::styling::semanticColor(palette, SemanticColorRole::StatusError);
    case WorkflowLogLevel::Info:
    default:
        return an::ui::styling::semanticColor(palette, SemanticColorRole::TextPrimary);
    }
}

bool shouldShowCompletionDialog()
{
    if (!QCoreApplication::instance())
    {
        return false;
    }

    if (qEnvironmentVariableIsSet("AFTERNIGHT_FORCE_MODAL_DIALOGS"))
    {
        return true;
    }

    if (qEnvironmentVariableIsSet("AFTERNIGHT_DISABLE_MODAL_DIALOGS"))
    {
        return false;
    }

    // Test binaries may override applicationName() during specific suites,
    // so also key off the executable filename and gtest environment.
    if (qEnvironmentVariableIsSet("GTEST_FILTER") || qEnvironmentVariableIsSet("GTEST_TOTAL_SHARDS") ||
        qEnvironmentVariableIsSet("GTEST_SHUFFLE"))
    {
        return false;
    }

    const QString appName = QCoreApplication::applicationName();
    const QString exeName = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
    if (appName.startsWith(QStringLiteral("Test_"), Qt::CaseInsensitive))
    {
        return false;
    }
    if (exeName.startsWith(QStringLiteral("Test_"), Qt::CaseInsensitive))
    {
        return false;
    }

    if (!QGuiApplication::instance())
    {
        return false;
    }

    const QString platform = QGuiApplication::platformName().toLower();
    return platform != QStringLiteral("offscreen") && platform != QStringLiteral("minimal") &&
           platform != QStringLiteral("minimalegl");
}

QString stageDisplayName(WorkflowStage stage)
{
    switch (stage)
    {
    case WorkflowStage::Bias:
        return QStringLiteral("Bias");
    case WorkflowStage::Dark:
        return QStringLiteral("Dark");
    case WorkflowStage::Flat:
        return QStringLiteral("Flat");
    case WorkflowStage::LightsCalibration:
        return QStringLiteral("Lights calibration");
    case WorkflowStage::LightsRegistration:
        return QStringLiteral("Registration");
    case WorkflowStage::LightsAlignment:
        return QStringLiteral("Alignment");
    case WorkflowStage::LightsNormalization:
        return QStringLiteral("Normalization");
    case WorkflowStage::LightsDrizzle:
        return QStringLiteral("Drizzle");
    case WorkflowStage::LightsStacking:
        return QStringLiteral("Stacking");
    case WorkflowStage::None:
        break;
    }
    return {};
}

void openCompletionDialog(QWidget* parent, QMessageBox::Icon icon, const QString& title, const QString& text)
{
    if (!shouldShowCompletionDialog())
    {
        return;
    }

    auto* messageBox = new QMessageBox(parent);
    messageBox->setObjectName(QStringLiteral("workflowCompletionMessageBox"));
    messageBox->setProperty("anDialogVariant", QStringLiteral("messageBox"));
    messageBox->setIcon(icon);
    messageBox->setWindowTitle(title);
    messageBox->setText(text);
    messageBox->setStandardButtons(QMessageBox::Ok);
    messageBox->setWindowModality(parent ? Qt::WindowModal : Qt::ApplicationModal);
    messageBox->setTextInteractionFlags(Qt::TextSelectableByMouse);
    if (auto* okButton = messageBox->button(QMessageBox::Ok))
    {
        okButton->setProperty("anButtonVariant", QStringLiteral("primary"));
    }

    QObject::connect(messageBox, &QMessageBox::finished, messageBox, &QObject::deleteLater);
    messageBox->open();
}
} // namespace

W_OBJECT_IMPL(WorkflowProgressDialogV2)

class WorkflowProgressDialogV2::Impl
{
public:
    WorkflowProgressController* controller;
    StageTimelineWidget* timeline;
    QProgressBar* stageProgressBar;
    QLabel* lblCurrentOp;
    QLabel* lblCurrentFileCaption;
    QLabel* lblCurrentFile;
    QLabel* lblTime;
    QLabel* lblLogTitle;
    QTextEdit* txtLog;
    QPushButton* btnCancel;
    QPushButton* btnClose;
    QPushButton* btnOpenReport;
    an::ui::AnCardWidget* grpLog;
    QTimer* timeUpdateTimer = nullptr;

    QElapsedTimer elapsedTimer;
    QStringList stageNames;
    bool finished = false;
    bool success = false;
    bool cancelled = false;
    bool failed = false;
    QString errorMessage;

    // Monotonic progress tracking — prevents the bar from going backwards
    // when engine sub-operations restart their internal 0–100 progress.
    float maxStageProgress = 0.0f;
    float lastOverallProgress = 0.0f;
    WorkflowStage lastProgressStage = WorkflowStage::None;
};

WorkflowProgressDialogV2::WorkflowProgressDialogV2(WorkflowProgressController* controller, QWidget* parent)
    : QDialog(parent), m_impl(std::make_unique<Impl>())
{
    setObjectName(QStringLiteral("workflowProgressDialogV2"));
    m_impl->controller = controller;
    setupUi();
    setupConnections();

    m_impl->elapsedTimer.start();
    updateTimeDisplay();
}

WorkflowProgressDialogV2::~WorkflowProgressDialogV2()
{
    QCoreApplication::removePostedEvents(this, QEvent::MetaCall);

    if (m_impl && m_impl->timeUpdateTimer)
    {
        m_impl->timeUpdateTimer->stop();
    }
}

void WorkflowProgressDialogV2::setupUi()
{
    setWindowTitle(tr("Image Integration - Processing"));
    setWindowModality(Qt::ApplicationModal);
    setModal(true);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setSizeGripEnabled(true);

    auto* layout = new QVBoxLayout(this);
    const auto& sp = an::ui::tokens().spacing;
    const QSize minimumDialogSize(sp.xl * 26, sp.xl * 16);
    setMinimumSize(minimumDialogSize);
    layout->setContentsMargins(sp.lg, sp.md, sp.lg, sp.lg);

    // 1. Timeline
    m_impl->timeline = new StageTimelineWidget(this);
    layout->addWidget(m_impl->timeline);

    // 2. Status & Progress
    auto* statusLayout = new QVBoxLayout();
    statusLayout->setContentsMargins(0, sp.sm, 0, sp.sm);

    m_impl->lblCurrentOp = new QLabel(tr("Initializing..."), this);
    m_impl->lblCurrentOp->setObjectName(QStringLiteral("lblCurrentOperation"));
    m_impl->lblCurrentOp->setFont(an::ui::tokens().typography.heading3);
    statusLayout->addWidget(m_impl->lblCurrentOp);

    m_impl->stageProgressBar = new QProgressBar(this);
    m_impl->stageProgressBar->setObjectName(QStringLiteral("stageProgressBar"));
    m_impl->stageProgressBar->setRange(0, 100);
    m_impl->stageProgressBar->setTextVisible(true);
    m_impl->stageProgressBar->setMinimumHeight(sp.xl);
    statusLayout->addWidget(m_impl->stageProgressBar);

    auto* fileLayout = new QHBoxLayout();
    m_impl->lblCurrentFileCaption = new QLabel(tr("Current File:"), this);
    m_impl->lblCurrentFileCaption->setObjectName(QStringLiteral("lblCurrentFileCaption"));
    fileLayout->addWidget(m_impl->lblCurrentFileCaption);
    m_impl->lblCurrentFile = new QLabel(tr("-"), this);
    m_impl->lblCurrentFile->setObjectName(QStringLiteral("lblCurrentFile"));
    m_impl->lblCurrentFile->setWordWrap(false);
    m_impl->lblCurrentFile->setTextInteractionFlags(Qt::TextSelectableByMouse);
    fileLayout->addWidget(m_impl->lblCurrentFile, 1);
    statusLayout->addLayout(fileLayout);

    layout->addLayout(statusLayout);

    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    separator->setFixedHeight(1);
    layout->addWidget(separator);

    // 3. Log (Always Expanded) — dark console-style panel
    m_impl->grpLog = new an::ui::AnCardWidget(QString(), this);
    auto* logLayout = new QVBoxLayout();
    m_impl->grpLog->setContentLayout(logLayout);
    logLayout->setContentsMargins(sp.sm, sp.sm, sp.sm, sp.sm);
    m_impl->lblLogTitle = new QLabel(tr("Process Output"), this);
    m_impl->lblLogTitle->setObjectName(QStringLiteral("lblLogTitle"));
    logLayout->addWidget(m_impl->lblLogTitle);
    m_impl->txtLog = new QTextEdit(this);
    m_impl->txtLog->setObjectName(QStringLiteral("workflowLogText"));
    m_impl->txtLog->setReadOnly(true);
    m_impl->txtLog->setFont(an::ui::tokens().typography.monospace);
    m_impl->txtLog->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    logLayout->addWidget(m_impl->txtLog);
    layout->addWidget(m_impl->grpLog, 1); // Takes remaining space

    // 4. Footer (Time & Buttons)
    auto* footerLayout = new QHBoxLayout();
    footerLayout->setContentsMargins(0, sp.sm, 0, 0);
    m_impl->lblTime = new QLabel(this);
    m_impl->lblTime->setObjectName(QStringLiteral("lblElapsed"));
    m_impl->lblTime->setFont(an::ui::tokens().typography.body);
    footerLayout->addWidget(m_impl->lblTime);
    footerLayout->addStretch();

    m_impl->btnCancel = new QPushButton(tr("Cancel"), this);
    m_impl->btnCancel->setObjectName(QStringLiteral("btnCancel"));
    m_impl->btnClose = new QPushButton(tr("Close"), this);
    m_impl->btnClose->setObjectName(QStringLiteral("btnClose"));
    m_impl->btnClose->setVisible(false);
    m_impl->btnClose->setDefault(true);

    m_impl->btnOpenReport = new QPushButton(tr("Open Report"), this);
    m_impl->btnOpenReport->setObjectName(QStringLiteral("btnOpenReport"));
    m_impl->btnOpenReport->setVisible(false);

    footerLayout->addWidget(m_impl->btnOpenReport);
    footerLayout->addWidget(m_impl->btnCancel);
    footerLayout->addWidget(m_impl->btnClose);
    layout->addLayout(footerLayout);

    applyTheme();
}

void WorkflowProgressDialogV2::setupConnections()
{
    connect(m_impl->btnCancel, &QPushButton::clicked, this, [this]() { requestCancellation(); });

    connect(m_impl->btnClose, &QPushButton::clicked, this, &QDialog::accept);

    connect(m_impl->btnOpenReport, &QPushButton::clicked, this, [this]() {
        const QString path = m_impl->btnOpenReport->property("reportPath").toString();
        if (!path.isEmpty())
        {
            an::ui::openLocalFile(path);
        }
    });

    if (m_impl->controller)
    {
        connect(m_impl->controller, &WorkflowProgressController::progressUpdated, this,
                &WorkflowProgressDialogV2::onProgressUpdated);
        connect(m_impl->controller, &WorkflowProgressController::logMessage, this,
                &WorkflowProgressDialogV2::onLogMessage);
        connect(m_impl->controller, &WorkflowProgressController::finished, this, &WorkflowProgressDialogV2::onFinished);
    }

    if (!m_impl->timeUpdateTimer)
    {
        m_impl->timeUpdateTimer = new QTimer(this);
        m_impl->timeUpdateTimer->setInterval(1000);
        connect(m_impl->timeUpdateTimer, &QTimer::timeout, this, [this]() { updateTimeDisplay(); });
        m_impl->timeUpdateTimer->start();
    }
}

void WorkflowProgressDialogV2::applyTheme()
{
    using an::ui::styling::SemanticColorRole;

    const auto themeVariant = an::ui::styling::detectThemeVariant(palette());
    const QColor surfaceWindow = an::ui::styling::semanticColor(themeVariant, SemanticColorRole::SurfaceWindow);
    const QColor surfaceElevated = an::ui::styling::semanticColor(themeVariant, SemanticColorRole::SurfaceElevated);
    const QColor surfaceBorder = an::ui::styling::semanticColor(themeVariant, SemanticColorRole::SurfaceBorder);
    const QColor textPrimary = an::ui::styling::semanticColor(themeVariant, SemanticColorRole::TextPrimary);
    const QColor textSecondary = an::ui::styling::semanticColor(themeVariant, SemanticColorRole::TextSecondary);
    const QColor textOnAccent = an::ui::styling::semanticColor(themeVariant, SemanticColorRole::TextOnAccent);
    const QColor accentPrimary = an::ui::styling::semanticColor(themeVariant, SemanticColorRole::AccentPrimary);
    const QColor accentHover = an::ui::styling::semanticColor(themeVariant, SemanticColorRole::AccentHover);
    const QColor highlightBackground =
        an::ui::styling::semanticColor(themeVariant, SemanticColorRole::HighlightBackground);
    const QColor logBackground = blendColor(surfaceWindow, surfaceElevated, 0.85);

    if (m_impl->lblCurrentFileCaption)
    {
        QPalette p = m_impl->lblCurrentFileCaption->palette();
        p.setColor(QPalette::WindowText, textSecondary);
        m_impl->lblCurrentFileCaption->setPalette(p);
    }
    if (m_impl->lblLogTitle)
    {
        QPalette p = m_impl->lblLogTitle->palette();
        p.setColor(QPalette::WindowText, textSecondary);
        m_impl->lblLogTitle->setPalette(p);
        m_impl->lblLogTitle->setFont(an::ui::tokens().typography.heading3);
    }
    if (m_impl->txtLog)
    {
        QPalette logPal = m_impl->txtLog->palette();
        logPal.setColor(QPalette::Base, logBackground);
        logPal.setColor(QPalette::Text, textPrimary);
        logPal.setColor(QPalette::Highlight, highlightBackground);
        logPal.setColor(QPalette::HighlightedText, textPrimary);
        logPal.setColor(QPalette::Mid, surfaceBorder);
        m_impl->txtLog->setPalette(logPal);
        m_impl->txtLog->setProperty("anFrameRadius", an::ui::tokens().radius.sm);
        m_impl->txtLog->document()->setDocumentMargin(an::ui::tokens().spacing.sm);
    }
    if (m_impl->btnOpenReport)
    {
        m_impl->btnOpenReport->setProperty("anButtonVariant", QStringLiteral("primary"));
    }
}

void WorkflowProgressDialogV2::requestCancellation()
{
    if (m_impl->finished || !m_impl->btnCancel || !m_impl->btnCancel->isEnabled())
    {
        return;
    }

    m_impl->btnCancel->setEnabled(false);
    m_impl->btnCancel->setText(tr("Cancelling..."));
    emit cancelRequested();
    if (m_impl->controller)
    {
        m_impl->controller->cancel();
    }
}

void WorkflowProgressDialogV2::setStages(const QStringList& stages)
{
    m_impl->stageNames = stages;
    m_impl->timeline->setStages(stages);
}

void WorkflowProgressDialogV2::onProgressUpdated(const WorkflowProgress& progress)
{
    if (m_impl->finished)
        return;

    const int stageIdx = stageIndexFor(progress.currentStage);
    if (stageIdx >= 0)
    {
        m_impl->timeline->setCurrentStage(stageIdx);
        for (int i = 0; i < stageIdx; ++i)
        {
            if (m_impl->timeline->stageStatus(i) == StageTimelineStatus::Pending ||
                m_impl->timeline->stageStatus(i) == StageTimelineStatus::InProgress)
            {
                m_impl->timeline->setStageStatus(i, StageTimelineStatus::Completed);
            }
        }
        m_impl->timeline->setStageStatus(stageIdx, StageTimelineStatus::InProgress);
    }

    // Monotonic progress: reset maximum when a new stage begins, then never
    // allow the displayed value to decrease within the same stage.  This
    // prevents the bar from jumping backwards when the engine restarts its
    // internal progress counter between sub-operations.
    if (progress.currentStage != m_impl->lastProgressStage)
    {
        m_impl->maxStageProgress = 0.0f;
        m_impl->lastProgressStage = progress.currentStage;
    }
    const float displayProgress = (std::max)(m_impl->maxStageProgress, progress.overallProgress);
    m_impl->maxStageProgress = displayProgress;
    m_impl->lastOverallProgress = displayProgress;
    m_impl->stageProgressBar->setValue(static_cast<int>(displayProgress * 100.0f));

    // Operation label — include sub-step context when available
    if (!progress.currentOperation.isEmpty())
    {
        QString operationText = progress.currentOperation;
        if (progress.totalGroups > 0 && progress.currentGroup > 0)
        {
            operationText =
                QStringLiteral("[%1/%2] - %3").arg(progress.currentGroup).arg(progress.totalGroups).arg(operationText);
        }

        if (progress.totalSubSteps > 0 && progress.currentSubStep > 0)
        {
            const QString stepLabel = progress.subStepName.isEmpty()
                                          ? tr("Step %1/%2").arg(progress.currentSubStep).arg(progress.totalSubSteps)
                                          : tr("Step %1/%2: %3")
                                                .arg(progress.currentSubStep)
                                                .arg(progress.totalSubSteps)
                                                .arg(progress.subStepName);
            m_impl->lblCurrentOp->setText(QStringLiteral("%1  [%2]").arg(operationText, stepLabel));
        }
        else
        {
            m_impl->lblCurrentOp->setText(operationText);
        }
    }

    // Always update current-file label so it never gets stuck on a stale value.
    if (!progress.currentFile.isEmpty())
    {
        m_impl->lblCurrentFile->setText(QFileInfo(progress.currentFile).fileName());
    }
    else
    {
        m_impl->lblCurrentFile->setText(QStringLiteral("-"));
    }

    updateTimeDisplay();
}

void WorkflowProgressDialogV2::onLogMessage(WorkflowLogLevel level, const QString& message)
{
    // Timestamp prefix — always shown in dim gray.
    const QString ts = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));

    const auto themeVariant = an::ui::styling::detectThemeVariant(palette());
    const QColor textColorValue = workflowLogColor(palette(), level);
    const QColor textMuted =
        an::ui::styling::semanticColor(themeVariant, an::ui::styling::SemanticColorRole::TextMuted);
    const QColor rowBackground = blendColor(
        an::ui::styling::semanticColor(themeVariant, an::ui::styling::SemanticColorRole::SurfaceWindow),
        an::ui::styling::semanticColor(themeVariant, an::ui::styling::SemanticColorRole::SurfaceElevated), 0.78);

    const QString textColor = toCssColor(textColorValue);
    const QString timestampColor = toCssColor(textMuted);
    const QString rowBackgroundCss = toCssColor(rowBackground);
    const QString tsFontSize = QString::number(an::ui::tokens().typography.caption.pointSizeF(), 'f', 1);

    QString prefix;
    bool bold = false;

    switch (level)
    {
    case WorkflowLogLevel::Header:
        bold = true;
        break;
    case WorkflowLogLevel::Progress:
    case WorkflowLogLevel::Detail:
        break;
    case WorkflowLogLevel::Success:
        prefix = QStringLiteral("✓ ");
        break;
    case WorkflowLogLevel::Warning:
        prefix = QStringLiteral("⚠ ");
        break;
    case WorkflowLogLevel::Error:
        prefix = QStringLiteral("✗ ");
        bold = true;
        break;
    case WorkflowLogLevel::Info:
    default:
        break;
    }

    // Clean leading newlines the logger may embed for visual spacing.
    QString cleanMsg = message;
    while (cleanMsg.startsWith(QLatin1Char('\n')))
        cleanMsg = cleanMsg.mid(1);

    // Detect stage-level separators for special rendering.
    const bool isStageHeader = (level == WorkflowLogLevel::Header) && cleanMsg.contains(QStringLiteral("STAGE:"));

    // Detect workflow-level banners (start / complete / cancelled).
    const bool isWorkflowBanner = cleanMsg.contains(QStringLiteral("WORKFLOW"));

    QString fontStyle = QStringLiteral("color:%1").arg(textColor);
    if (bold)
        fontStyle += QStringLiteral(";font-weight:bold");

    // Build the HTML line.  Qt rich-text supports <table> with cell
    // background-color, which we use for stage headers to create a
    // visually distinct row without relying on unsupported CSS props.
    QString html;

    if (isWorkflowBanner || isStageHeader)
    {
        // Prominent row: subtle dark background + left accent via a
        // narrow coloured cell that acts as a visual border.
        html =
            QStringLiteral("<table width='100%' cellspacing='0' cellpadding='0' "
                           "style='margin-top:6px;margin-bottom:2px;'>"
                           "<tr>"
                           "<td width='3' style='background-color:%1;'></td>"
                           "<td style='background-color:%2;padding:4px 8px;'>"
                           "<span style='color:%3;font-size:%7pt;'>%4&nbsp;</span>"
                           "<span style='%5'>%6</span>"
                           "</td></tr></table>")
                .arg(textColor, rowBackgroundCss, timestampColor, ts, fontStyle, cleanMsg.toHtmlEscaped(), tsFontSize);
    }
    else
    {
        // Standard line — timestamp + coloured text.
        html = QStringLiteral("<p style='margin-top:0px;margin-bottom:1px;white-space:pre;'>"
                              "<span style='color:%1;font-size:%5pt;'>%2&nbsp;</span>"
                              "<span style='%3'>%4</span>"
                              "</p>")
                   .arg(timestampColor, ts, fontStyle, QStringLiteral("%1%2").arg(prefix, cleanMsg.toHtmlEscaped()),
                        tsFontSize);
    }

    m_impl->txtLog->append(html);

    // Auto-scroll to keep latest output visible.
    QScrollBar* sb = m_impl->txtLog->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void WorkflowProgressDialogV2::onFinished(const WorkflowExecutor::Result& result)
{
    using an::ui::styling::SemanticColorRole;

    m_impl->finished = true;
    m_impl->success = result.success;
    m_impl->cancelled = result.cancelled;
    m_impl->failed = !result.success && !result.cancelled;
    m_impl->errorMessage = result.errorMessage;
    m_impl->btnCancel->setVisible(false);
    m_impl->btnClose->setVisible(true);
    if (result.success)
    {
        m_impl->lastOverallProgress = 1.0f;
        m_impl->stageProgressBar->setValue(100);
    }
    applyTheme();
    if (m_impl->timeUpdateTimer)
    {
        m_impl->timeUpdateTimer->stop();
    }

    // Show "Open Report" button if the HTML report was generated
    if (!result.htmlReportPath.isEmpty())
    {
        m_impl->btnOpenReport->setVisible(true);
        m_impl->btnOpenReport->setProperty("reportPath", result.htmlReportPath);
        m_impl->btnOpenReport->setToolTip(
            tr("Open the workflow report in your default browser\n%1").arg(result.htmlReportPath));
    }

    if (result.success)
    {
        m_impl->lblCurrentOp->setText(tr("Workflow Completed Successfully"));
        for (int i = 0; i < static_cast<int>(m_impl->stageNames.size()); ++i)
        {
            m_impl->timeline->setStageStatus(i, StageTimelineStatus::Completed);
        }
        const QColor successColor = an::ui::styling::semanticColor(palette(), SemanticColorRole::StatusSuccess);
        const QColor successHover =
            blendColor(successColor, an::ui::styling::semanticColor(palette(), SemanticColorRole::AccentHover), 0.30);
        const QColor textOnAccent = an::ui::styling::semanticColor(palette(), SemanticColorRole::TextOnAccent);
        m_impl->btnClose->setProperty("anButtonVariant", QStringLiteral("primary"));
        {
            QPalette btnPal = m_impl->btnClose->palette();
            btnPal.setColor(QPalette::Button, successColor);
            btnPal.setColor(QPalette::ButtonText, textOnAccent);
            m_impl->btnClose->setPalette(btnPal);
        }

        if (result.partialSuccess && !result.errors.isEmpty())
        {
            // Completed with some per-frame/per-group errors
            m_impl->lblCurrentOp->setText(tr("Workflow Completed with Warnings"));
            QString details = tr("Workflow completed, but %1 frame(s)/group(s) had errors:\n\n")
                                  .arg(static_cast<int>(result.errors.size()));
            const qsizetype maxShown = 10;
            const qsizetype shownCount = (std::min)(result.errors.size(), maxShown);
            for (qsizetype i = 0; i < shownCount; ++i)
            {
                details += QStringLiteral("• %1\n").arg(result.errors[i]);
            }
            if (result.errors.size() > maxShown)
            {
                details += tr("\n... and %1 more error(s). See the workflow log for full details.")
                               .arg(static_cast<int>(result.errors.size() - maxShown));
            }
            openCompletionDialog(this, QMessageBox::Warning, tr("Completed with Errors"), details);
        }
        else
        {
            openCompletionDialog(this, QMessageBox::Information, tr("Complete"),
                                 tr("Workflow completed successfully."));
        }
    }
    else if (result.cancelled)
    {
        m_impl->lblCurrentOp->setText(tr("Workflow Cancelled"));
        openCompletionDialog(this, QMessageBox::Information, tr("Cancelled"), tr("Workflow was cancelled."));
    }
    else
    {
        m_impl->lblCurrentOp->setText(tr("Workflow Failed"));
        int current = m_impl->timeline->currentStage();
        if (current >= 0)
            m_impl->timeline->setStageStatus(current, StageTimelineStatus::Failed);
        const QString errorText = result.errorMessage.trimmed().isEmpty()
                                      ? tr("Workflow failed.")
                                      : tr("Workflow failed: %1").arg(result.errorMessage.trimmed());
        openCompletionDialog(this, QMessageBox::Critical, tr("Failed"), errorText);
    }
}

void WorkflowProgressDialogV2::reject()
{
    if (m_impl->finished)
    {
        QDialog::accept();
        return;
    }

    requestCancellation();
}

void WorkflowProgressDialogV2::closeEvent(QCloseEvent* event)
{
    if (m_impl->finished)
    {
        event->accept();
        return;
    }

    requestCancellation();
    event->ignore();
}

bool WorkflowProgressDialogV2::wasSuccessful() const
{
    return m_impl->success;
}

bool WorkflowProgressDialogV2::wasCancelled() const
{
    return m_impl->cancelled;
}

bool WorkflowProgressDialogV2::wasFailed() const
{
    return m_impl->failed;
}

void WorkflowProgressDialogV2::updateTimeDisplay()
{
    qint64 elapsed = m_impl->elapsedTimer.elapsed();
    QStringList parts;
    parts << tr("Elapsed: %1").arg(formatDuration(elapsed));

    const QString stageName = stageDisplayName(m_impl->lastProgressStage);
    if (!stageName.isEmpty())
    {
        parts << tr("Stage: %1").arg(stageName);
    }

    m_impl->lblTime->setText(parts.join(QStringLiteral("  |  ")));
}

int WorkflowProgressDialogV2::stageIndexFor(WorkflowStage stage) const
{
    QString stageName;
    switch (stage)
    {
    case WorkflowStage::Bias:
        stageName = "Bias";
        break;
    case WorkflowStage::Dark:
        stageName = "Dark";
        break;
    case WorkflowStage::Flat:
        stageName = "Flat";
        break;
    case WorkflowStage::LightsCalibration:
        stageName = "LightsCalibration";
        break;
    case WorkflowStage::LightsRegistration:
        stageName = "LightsRegistration";
        break;
    case WorkflowStage::LightsAlignment:
        stageName = "LightsAlignment";
        break;
    case WorkflowStage::LightsNormalization:
        stageName = "LightsNormalization";
        break;
    case WorkflowStage::LightsDrizzle:
        stageName = "LightsDrizzle";
        break;
    case WorkflowStage::LightsStacking:
        stageName = "LightsStacking";
        break;
    default:
        return -1;
    }

    for (int i = 0; i < m_impl->stageNames.size(); ++i)
    {
        if (m_impl->stageNames[i].contains(stageName, Qt::CaseInsensitive))
            return i;
    }

    return -1;
}

QString WorkflowProgressDialogV2::formatDuration(qint64 ms) const
{
    const qint64 totalSeconds = (std::max)(qint64{0}, ms / 1000);
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}
