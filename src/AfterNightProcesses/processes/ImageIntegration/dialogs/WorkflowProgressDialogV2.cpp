// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "WorkflowProgressDialogV2.h"
#include "theme/AnDesignTokens.h"
#include "processes/ImageIntegration/widgets/WorkflowRunView.h"
#include "processes/ImageIntegration/workflow/WorkflowProgressController.h"

#include <QCloseEvent>
#include <QVBoxLayout>
#include <QCoreApplication>
#include <QMessageBox>
#include <QFileInfo>
#include <QPushButton>

#include <QtGlobal>
#include <QGuiApplication>

#include <algorithm>

#include <verdigris/wobjectimpl.h>

namespace
{
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

WorkflowProgressDialogV2::WorkflowProgressDialogV2(WorkflowProgressController* controller, QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("workflowProgressDialogV2"));
    setWindowTitle(tr("Image Integration - Processing"));
    setWindowModality(Qt::ApplicationModal);
    setModal(true);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setSizeGripEnabled(true);

    const auto& sp = an::ui::tokens().spacing;
    const QSize minimumDialogSize(sp.xl * 26, sp.xl * 16);
    setMinimumSize(minimumDialogSize);

    // The view brings its own margins: the window adds none around it.
    auto* layout = new QVBoxLayout(this);
    layout->setProperty("anKeepLayoutMargins", true);
    layout->setProperty("anKeepLayoutSpacing", true);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_view = new WorkflowRunView(controller, this);
    layout->addWidget(m_view);

    // The window's own: Cancel is told on, Close closes it, and the run's end is said in a message over it.
    connect(m_view, &WorkflowRunView::cancelRequested, this, [this]() { emit cancelRequested(); });
    connect(m_view, &WorkflowRunView::closeRequested, this, &QDialog::accept);
    connect(m_view, &WorkflowRunView::runEnded, this,
            [this](const WorkflowExecutor::Result& result) { announceEnd(result); });
}

WorkflowProgressDialogV2::~WorkflowProgressDialogV2()
{
    QCoreApplication::removePostedEvents(this, QEvent::MetaCall);
}

WorkflowRunView* WorkflowProgressDialogV2::runView() const noexcept
{
    return m_view;
}

void WorkflowProgressDialogV2::setStages(const QStringList& stages)
{
    m_view->setStages(stages);
}

void WorkflowProgressDialogV2::onProgressUpdated(const WorkflowProgress& progress)
{
    m_view->onProgressUpdated(progress);
}

void WorkflowProgressDialogV2::onLogMessage(WorkflowLogLevel level, const QString& message)
{
    m_view->onLogMessage(level, message);
}

void WorkflowProgressDialogV2::onFinished(const WorkflowExecutor::Result& result)
{
    m_view->onFinished(result);
}

void WorkflowProgressDialogV2::announceEnd(const WorkflowExecutor::Result& result)
{
    if (result.success)
    {
        if (result.partialSuccess && !result.errors.isEmpty())
        {
            // Completed with some per-frame/per-group errors
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
        openCompletionDialog(this, QMessageBox::Information, tr("Cancelled"), tr("Workflow was cancelled."));
    }
    else
    {
        const QString errorText = result.errorMessage.trimmed().isEmpty()
                                      ? tr("Workflow failed.")
                                      : tr("Workflow failed: %1").arg(result.errorMessage.trimmed());
        openCompletionDialog(this, QMessageBox::Critical, tr("Failed"), errorText);
    }
}

void WorkflowProgressDialogV2::reject()
{
    if (m_view->isFinished())
    {
        QDialog::accept();
        return;
    }

    m_view->requestCancellation();
}

void WorkflowProgressDialogV2::closeEvent(QCloseEvent* event)
{
    if (m_view->isFinished())
    {
        event->accept();
        return;
    }

    m_view->requestCancellation();
    event->ignore();
}

bool WorkflowProgressDialogV2::wasSuccessful() const
{
    return m_view->wasSuccessful();
}

bool WorkflowProgressDialogV2::wasCancelled() const
{
    return m_view->wasCancelled();
}

bool WorkflowProgressDialogV2::wasFailed() const
{
    return m_view->wasFailed();
}
