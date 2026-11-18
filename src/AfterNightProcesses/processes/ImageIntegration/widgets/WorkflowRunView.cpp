// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "processes/ImageIntegration/widgets/WorkflowRunView.h"

#include "app/DesktopUtils.h"
#include "app/UiStyleTokens.h"
#include "components/AnCardWidget.h"
#include "processes/ImageIntegration/ui_models/IntegrationJobsModel.h"
#include "processes/ImageIntegration/workflow/WorkflowProgressController.h"
#include "processes/ImageIntegration/workflow/WorkflowTimeEstimator.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QTextEdit>
#include <QTextOption>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(WorkflowRunView)

namespace
{

using an::ui::StageRow;
using an::ui::StageStatus;
using an::ui::StageStep;

constexpr std::array kStages = {
    WorkflowStage::Bias,
    WorkflowStage::Dark,
    WorkflowStage::Flat,
    WorkflowStage::LightsCalibration,
    WorkflowStage::LightsRegistration,
    WorkflowStage::LightsAlignment,
    WorkflowStage::LightsNormalization,
    WorkflowStage::LightsDrizzle,
    WorkflowStage::LightsStacking,
};

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

/// The stage a name of a stage sequence names: the one whose own name it is, or else the first whose name it holds;
/// none for a name of no stage - "Preparing".
WorkflowStage stageOfToken(const QString& token)
{
    for (const WorkflowStage stage : kStages)
    {
        if (ImageIntegration::workflowStageToken(stage) == token)
        {
            return stage;
        }
    }
    // The longer names first: "LightsStacking" is not "Bias" for holding no part of it, but a name is tried whole.
    for (auto stage = kStages.rbegin(); stage != kStages.rend(); ++stage)
    {
        if (token.contains(ImageIntegration::workflowStageToken(*stage), Qt::CaseInsensitive))
        {
            return *stage;
        }
    }
    return WorkflowStage::None;
}

QString formatClock(const qint64 ms)
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

} // namespace

class WorkflowRunView::Impl
{
public:
    QPointer<WorkflowProgressController> controller;
    an::ui::AnStageList* stages = nullptr;
    QProgressBar* runProgressBar = nullptr;
    QLabel* lblCurrentOp = nullptr;
    QLabel* lblCurrentFileCaption = nullptr;
    QLabel* lblCurrentFile = nullptr;
    QLabel* lblTime = nullptr;
    QLabel* lblRemaining = nullptr;
    QLabel* lblLogTitle = nullptr;
    QTextEdit* txtLog = nullptr;
    QPushButton* btnCancel = nullptr;
    QPushButton* btnClose = nullptr;
    QPushButton* btnOpenReport = nullptr;
    an::ui::AnCardWidget* grpLog = nullptr;
    QTimer* timeUpdateTimer = nullptr;

    QElapsedTimer elapsedTimer;
    QStringList stageNames;
    std::vector<double> stageWeights;
    std::vector<StageRow> rows;
    bool finished = false;
    bool success = false;
    bool cancelled = false;
    bool failed = false;
    QString reportPath;

    /// How long the run took, as it said when it ended; none while it runs and for a run that did not say.
    std::optional<qint64> endedDurationMs;

    // Monotonic progress tracking - prevents the bars from going backwards when engine sub-operations restart their
    // internal 0-100 progress.
    float maxStageProgress = 0.0f;
    double maxRunFraction = 0.0;
    WorkflowStage lastProgressStage = WorkflowStage::None;
    int currentRow = -1;

    /// The parts of what the stage on its way does to a group, as the run has named them so far.
    QStringList stepNames;

    ImageIntegration::WorkflowRunStanding standing;
};

WorkflowRunView::WorkflowRunView(WorkflowProgressController* controller, QWidget* parent)
    : QWidget(parent), m_impl(std::make_unique<Impl>())
{
    setObjectName(QStringLiteral("workflowRunView"));
    m_impl->controller = controller;
    setupUi();
    setupConnections();

    m_impl->elapsedTimer.start();
    updateTimeDisplay();
}

WorkflowRunView::~WorkflowRunView()
{
    QCoreApplication::removePostedEvents(this, QEvent::MetaCall);

    if (m_impl && m_impl->timeUpdateTimer)
    {
        m_impl->timeUpdateTimer->stop();
    }
}

void WorkflowRunView::setupUi()
{
    const auto& sp = an::ui::tokens().spacing;
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(sp.lg, sp.md, sp.lg, sp.lg);

    auto* body = new QHBoxLayout();
    body->setSpacing(sp.lg);

    // 1. The run's stages, a list.
    m_impl->stages = new an::ui::AnStageList(this);
    m_impl->stages->setObjectName(QStringLiteral("runStageList"));
    m_impl->stages->setAccessibleName(tr("Stages of the run"));
    // The rows say where the run is; nothing is chosen in them.
    m_impl->stages->setSelectionMode(QAbstractItemView::NoSelection);
    m_impl->stages->setFocusPolicy(Qt::NoFocus);
    m_impl->stages->setFixedWidth(an::ui::parts::kStageColumnWidth);
    body->addWidget(m_impl->stages);

    auto* right = new QVBoxLayout();

    // 2. Status & Progress
    auto* statusLayout = new QVBoxLayout();
    statusLayout->setContentsMargins(0, 0, 0, sp.sm);

    m_impl->lblCurrentOp = new QLabel(tr("Initializing..."), this);
    m_impl->lblCurrentOp->setObjectName(QStringLiteral("lblCurrentOperation"));
    m_impl->lblCurrentOp->setFont(an::ui::tokens().typography.heading3);
    statusLayout->addWidget(m_impl->lblCurrentOp);

    m_impl->runProgressBar = new QProgressBar(this);
    m_impl->runProgressBar->setObjectName(QStringLiteral("stageProgressBar"));
    m_impl->runProgressBar->setRange(0, 100);
    m_impl->runProgressBar->setTextVisible(true);
    m_impl->runProgressBar->setMinimumHeight(sp.xl);
    statusLayout->addWidget(m_impl->runProgressBar);

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

    right->addLayout(statusLayout);

    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    separator->setFixedHeight(1);
    right->addWidget(separator);

    // 3. Log (Always Expanded) - console-style panel
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
    right->addWidget(m_impl->grpLog, 1); // Takes remaining space

    body->addLayout(right, 1);
    layout->addLayout(body, 1);

    // 4. Footer (Times & Buttons)
    auto* footerLayout = new QHBoxLayout();
    footerLayout->setContentsMargins(0, sp.sm, 0, 0);
    m_impl->lblTime = new QLabel(this);
    m_impl->lblTime->setObjectName(QStringLiteral("lblElapsed"));
    m_impl->lblTime->setFont(an::ui::tokens().typography.body);
    footerLayout->addWidget(m_impl->lblTime);
    m_impl->lblRemaining = new QLabel(this);
    m_impl->lblRemaining->setObjectName(QStringLiteral("lblRemaining"));
    m_impl->lblRemaining->setFont(an::ui::tokens().typography.body);
    m_impl->lblRemaining->setVisible(false);
    footerLayout->addSpacing(sp.lg);
    footerLayout->addWidget(m_impl->lblRemaining);
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

void WorkflowRunView::setupConnections()
{
    connect(m_impl->btnCancel, &QPushButton::clicked, this, [this]() { requestCancellation(); });

    connect(m_impl->btnClose, &QPushButton::clicked, this, [this]() { emit closeRequested(); });

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
                &WorkflowRunView::onProgressUpdated);
        connect(m_impl->controller, &WorkflowProgressController::logMessage, this, &WorkflowRunView::onLogMessage);
        connect(m_impl->controller, &WorkflowProgressController::finished, this, &WorkflowRunView::onFinished);
    }

    if (!m_impl->timeUpdateTimer)
    {
        m_impl->timeUpdateTimer = new QTimer(this);
        m_impl->timeUpdateTimer->setInterval(1000);
        connect(m_impl->timeUpdateTimer, &QTimer::timeout, this, [this]() { updateTimeDisplay(); });
        m_impl->timeUpdateTimer->start();
    }
}

void WorkflowRunView::applyTheme()
{
    using an::ui::styling::SemanticColorRole;

    const auto themeVariant = an::ui::styling::detectThemeVariant(palette());
    const QColor surfaceWindow = an::ui::styling::semanticColor(themeVariant, SemanticColorRole::SurfaceWindow);
    const QColor surfaceElevated = an::ui::styling::semanticColor(themeVariant, SemanticColorRole::SurfaceElevated);
    const QColor surfaceBorder = an::ui::styling::semanticColor(themeVariant, SemanticColorRole::SurfaceBorder);
    const QColor textPrimary = an::ui::styling::semanticColor(themeVariant, SemanticColorRole::TextPrimary);
    const QColor textSecondary = an::ui::styling::semanticColor(themeVariant, SemanticColorRole::TextSecondary);
    const QColor highlightBackground =
        an::ui::styling::semanticColor(themeVariant, SemanticColorRole::HighlightBackground);
    const QColor logBackground = blendColor(surfaceWindow, surfaceElevated, 0.85);

    for (QLabel* const quiet : {m_impl->lblCurrentFileCaption, m_impl->lblRemaining})
    {
        if (quiet)
        {
            QPalette p = quiet->palette();
            p.setColor(QPalette::WindowText, textSecondary);
            quiet->setPalette(p);
        }
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

void WorkflowRunView::refreshTheme()
{
    applyTheme();
    if (m_impl->stages)
    {
        m_impl->stages->viewport()->update();
    }
    update();
}

void WorkflowRunView::requestCancellation()
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

void WorkflowRunView::setStages(const QStringList& stages, const std::vector<double>& weights)
{
    m_impl->stageNames = stages;
    m_impl->stageWeights = weights;
    m_impl->rows.clear();
    m_impl->rows.reserve(static_cast<std::size_t>(stages.size()));
    for (const QString& name : stages)
    {
        StageRow row;
        row.id = name;
        const QString title = ImageIntegration::workflowStageTitle(stageOfToken(name));
        row.title = title.isEmpty() ? name : title;
        m_impl->rows.push_back(std::move(row));
    }
    // The stages are told again once a preparation has made them: what was said of the ones before is gone.
    m_impl->currentRow = -1;
    m_impl->lastProgressStage = WorkflowStage::None;
    m_impl->maxStageProgress = 0.0f;
    m_impl->maxRunFraction = 0.0;
    m_impl->stepNames.clear();
    m_impl->standing = ImageIntegration::WorkflowRunStanding{.steps = static_cast<int>(stages.size())};
    publishStageRows();
}

void WorkflowRunView::publishStageRows()
{
    // One row at a time where the stages are the ones shown, so a row that did not change is not laid out again.
    const std::vector<StageRow> shown = m_impl->stages->rows();
    bool sameStages = shown.size() == m_impl->rows.size();
    for (std::size_t row = 0; sameStages && row < shown.size(); ++row)
    {
        sameStages = shown[row].id == m_impl->rows[row].id;
    }
    if (!sameStages)
    {
        m_impl->stages->setRows(m_impl->rows);
    }
    else
    {
        for (std::size_t row = 0; row < shown.size(); ++row)
        {
            if (!(shown[row] == m_impl->rows[row]))
            {
                m_impl->stages->updateRow(m_impl->rows[row]);
            }
        }
    }
    if (m_impl->currentRow >= 0 && m_impl->currentRow < m_impl->stages->rowCount())
    {
        m_impl->stages->scrollTo(m_impl->stages->model()->index(m_impl->currentRow, 0));
    }
}

void WorkflowRunView::onProgressUpdated(const WorkflowProgress& progress)
{
    if (m_impl->finished)
        return;

    Impl& impl = *m_impl;

    // Monotonic progress: reset maximum when a new stage begins, then never allow the displayed value to decrease
    // within the same stage. This prevents the bars from jumping backwards when the engine restarts its internal
    // progress counter between sub-operations.
    if (progress.currentStage != impl.lastProgressStage)
    {
        impl.maxStageProgress = 0.0f;
        impl.lastProgressStage = progress.currentStage;
        impl.stepNames.clear();
    }
    const float stageProgress = (std::max)(impl.maxStageProgress, progress.overallProgress);
    impl.maxStageProgress = stageProgress;

    WorkflowProgress shown = progress;
    shown.overallProgress = stageProgress;
    ImageIntegration::WorkflowRunStanding standing =
        ImageIntegration::workflowRunStanding(impl.stageNames, impl.stageWeights, shown);
    // The whole never goes back while the run stays among its stages.
    if (standing.step > 0)
    {
        standing.fraction = (std::max)(standing.fraction, impl.maxRunFraction);
        impl.maxRunFraction = standing.fraction;
    }
    impl.runProgressBar->setValue(static_cast<int>(std::lround(standing.fraction * 100.0)));

    // The stages: the ones the run has passed are done, the one it is at carries its progress and its parts.
    const int row = standing.step - 1;
    if (row >= 0 && row < static_cast<int>(impl.rows.size()))
    {
        impl.currentRow = row;
        for (int before = 0; before < row; ++before)
        {
            StageRow& passed = impl.rows[static_cast<std::size_t>(before)];
            if (passed.status == StageStatus::Pending || passed.status == StageStatus::Running)
            {
                passed.status = StageStatus::Done;
                passed.line = tr("done");
                passed.progress.reset();
                passed.steps.clear();
            }
        }
        StageRow& at = impl.rows[static_cast<std::size_t>(row)];
        at.status = StageStatus::Running;
        at.progress = static_cast<int>(std::lround(standing.stageFraction * 100.0));
        at.line = progress.totalGroups > 1 && progress.currentGroup > 0
                      ? tr("group %1 of %2").arg(progress.currentGroup).arg(progress.totalGroups)
                      : tr("running");
        at.steps.clear();
        if (progress.totalSubSteps > 0 && progress.currentSubStep > 0)
        {
            if (impl.stepNames.size() != progress.totalSubSteps)
            {
                impl.stepNames = QStringList(progress.totalSubSteps);
            }
            const int stepAt = (std::min)(progress.currentSubStep, progress.totalSubSteps) - 1;
            if (!progress.subStepName.isEmpty())
            {
                impl.stepNames[stepAt] = progress.subStepName;
            }
            for (int step = 0; step < progress.totalSubSteps; ++step)
            {
                const QString& named = impl.stepNames[step];
                at.steps.push_back(StageStep{named.isEmpty() ? tr("Step %1").arg(step + 1) : named,
                                             step < stepAt
                                                 ? StageStatus::Done
                                                 : (step == stepAt ? StageStatus::Running : StageStatus::Pending)});
            }
        }
        publishStageRows();
    }

    // Operation label - include sub-step context when available
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
            impl.lblCurrentOp->setText(QStringLiteral("%1  [%2]").arg(operationText, stepLabel));
        }
        else
        {
            impl.lblCurrentOp->setText(operationText);
        }
    }

    // Always update current-file label so it never gets stuck on a stale value.
    if (!progress.currentFile.isEmpty())
    {
        impl.lblCurrentFile->setText(QFileInfo(progress.currentFile).fileName());
    }
    else
    {
        impl.lblCurrentFile->setText(QStringLiteral("-"));
    }

    const bool moved = !(standing == impl.standing);
    impl.standing = standing;
    updateTimeDisplay();
    if (moved)
    {
        emit standingChanged();
    }
}

void WorkflowRunView::onLogMessage(WorkflowLogLevel level, const QString& message)
{
    // Timestamp prefix - always shown in dim gray.
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
        // Standard line - timestamp + coloured text.
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

void WorkflowRunView::onFinished(const WorkflowExecutor::Result& result)
{
    using an::ui::styling::SemanticColorRole;

    if (m_impl->finished)
    {
        return;
    }
    Impl& impl = *m_impl;
    impl.finished = true;
    impl.success = result.success;
    impl.cancelled = result.cancelled;
    impl.failed = !result.success && !result.cancelled;
    impl.btnCancel->setVisible(false);
    impl.btnClose->setVisible(true);
    if (result.success)
    {
        impl.runProgressBar->setValue(100);
    }
    applyTheme();
    if (impl.timeUpdateTimer)
    {
        impl.timeUpdateTimer->stop();
    }
    if (result.totalDurationMs > 0)
    {
        impl.endedDurationMs = result.totalDurationMs;
    }
    showRemaining(std::nullopt);

    // Show "Open Report" button if the HTML report was generated
    if (!result.htmlReportPath.isEmpty())
    {
        impl.reportPath = result.htmlReportPath;
        impl.btnOpenReport->setVisible(true);
        impl.btnOpenReport->setProperty("reportPath", result.htmlReportPath);
        impl.btnOpenReport->setToolTip(
            tr("Open the workflow report in your default browser\n%1").arg(result.htmlReportPath));
    }

    // The time the run says each stage took, by the stage's row.
    const auto tookOf = [&result](const StageRow& row) -> std::optional<qint64> {
        const WorkflowStage stage = stageOfToken(row.id);
        for (const WorkflowExecutor::Result::StageTiming& timing : result.stageResults)
        {
            if (timing.stageName == row.id || (stage != WorkflowStage::None && stageOfToken(timing.stageName) == stage))
            {
                return timing.durationMs;
            }
        }
        return std::nullopt;
    };
    const auto doneLine = [this, &tookOf](const StageRow& row) {
        const std::optional<qint64> took = tookOf(row);
        return took.has_value() ? tr("done · %1").arg(ImageIntegration::integrationJobDurationText(*took)) : tr("done");
    };

    if (result.success)
    {
        impl.lblCurrentOp->setText(tr("Workflow Completed Successfully"));
        for (StageRow& row : impl.rows)
        {
            row.status = StageStatus::Done;
            row.line = doneLine(row);
            row.progress.reset();
            row.steps.clear();
        }
        const QColor successColor = an::ui::styling::semanticColor(palette(), SemanticColorRole::StatusSuccess);
        const QColor textOnAccent = an::ui::styling::semanticColor(palette(), SemanticColorRole::TextOnAccent);
        impl.btnClose->setProperty("anButtonVariant", QStringLiteral("primary"));
        {
            QPalette btnPal = impl.btnClose->palette();
            btnPal.setColor(QPalette::Button, successColor);
            btnPal.setColor(QPalette::ButtonText, textOnAccent);
            impl.btnClose->setPalette(btnPal);
        }
        if (result.partialSuccess && !result.errors.isEmpty())
        {
            // Completed with some per-frame/per-group errors
            impl.lblCurrentOp->setText(tr("Workflow Completed with Warnings"));
        }
    }
    else
    {
        impl.lblCurrentOp->setText(result.cancelled ? tr("Workflow Cancelled") : tr("Workflow Failed"));
        // The stages the run passed are done; the one it stopped at says how it stopped.
        for (int row = 0; row < static_cast<int>(impl.rows.size()); ++row)
        {
            StageRow& at = impl.rows[static_cast<std::size_t>(row)];
            at.progress.reset();
            at.steps.clear();
            if (row < impl.currentRow)
            {
                at.status = StageStatus::Done;
                at.line = doneLine(at);
            }
            else if (row == impl.currentRow)
            {
                at.status = result.cancelled ? StageStatus::Warning : StageStatus::Failed;
                at.line = result.cancelled ? tr("stopped") : tr("failed");
            }
        }
    }
    publishStageRows();
    updateTimeDisplay();
    emit runEnded(result);
}

bool WorkflowRunView::isFinished() const
{
    return m_impl->finished;
}

bool WorkflowRunView::wasSuccessful() const
{
    return m_impl->success;
}

bool WorkflowRunView::wasCancelled() const
{
    return m_impl->cancelled;
}

bool WorkflowRunView::wasFailed() const
{
    return m_impl->failed;
}

const ImageIntegration::WorkflowRunStanding& WorkflowRunView::standing() const noexcept
{
    return m_impl->standing;
}

std::vector<an::ui::StageRow> WorkflowRunView::stageRows() const
{
    return m_impl->rows;
}

QString WorkflowRunView::reportPath() const
{
    return m_impl->reportPath;
}

void WorkflowRunView::showRemaining(const std::optional<qint64> remainingMs)
{
    if (!remainingMs.has_value() || m_impl->finished)
    {
        m_impl->lblRemaining->setVisible(false);
        return;
    }
    m_impl->lblRemaining->setText(
        tr("Remaining: about %1")
            .arg(WorkflowTimeEstimator::formatDuration(static_cast<int>((*remainingMs + 500) / 1000))));
    m_impl->lblRemaining->setVisible(true);
}

void WorkflowRunView::updateTimeDisplay()
{
    const qint64 elapsed =
        m_impl->endedDurationMs.has_value() ? *m_impl->endedDurationMs : m_impl->elapsedTimer.elapsed();
    QStringList parts;
    parts << tr("Elapsed: %1").arg(formatClock(elapsed));

    const QString stageName = ImageIntegration::workflowStageTitle(m_impl->lastProgressStage);
    if (!stageName.isEmpty())
    {
        parts << tr("Stage: %1").arg(stageName);
    }

    m_impl->lblTime->setText(parts.join(QStringLiteral("  |  ")));
    if (!m_impl->finished)
    {
        showRemaining(ImageIntegration::workflowRemainingMs(elapsed, m_impl->standing.fraction));
    }
}
