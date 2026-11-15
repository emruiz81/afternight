// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "workspaces/WorkspaceWindow.h"

#include "pages/WorkspacePage.h"
#include "workspaces/WorkspaceToolbar.h"

#include "theme/ComponentParts.h"

#include <QAbstractButton>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

namespace an::ui::observatory
{

WorkspaceWindow::WorkspaceWindow(const QString& title, WorkspaceToolbar& header, WorkspacePage& page, Hooks hooks)
    : QWidget(nullptr, Qt::Window), m_hooks(std::move(hooks)), m_header(&header), m_page(&page)
{
    setObjectName(QStringLiteral("observatoryWorkspaceWindow"));
    setWindowTitle(title);
    setAcceptDrops(true);
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    // The header: the layout icons at its right, as the top bar holds them while a workspace is shown.
    auto* headerRow = new QHBoxLayout();
    headerRow->setContentsMargins(an::ui::parts::kTopBarPadding, an::ui::parts::kTopBarGap / 2,
                                  an::ui::parts::kTopBarPadding, an::ui::parts::kTopBarGap / 2);
    headerRow->addStretch(1);
    m_header->setParent(this);
    headerRow->addWidget(m_header);
    m_layout->addLayout(headerRow);

    m_page->setParent(this);
    m_layout->addWidget(m_page, 1);
    m_page->show();
}

WorkspaceWindow::~WorkspaceWindow()
{
    m_hooks = {};
}

WorkspacePage* WorkspaceWindow::takePage()
{
    WorkspacePage* const taken = m_page.data();
    if (taken != nullptr)
    {
        m_layout->removeWidget(taken);
        taken->setParent(nullptr);
        m_page.clear();
    }
    return taken;
}

WorkspacePage* WorkspaceWindow::page() const noexcept
{
    return m_page.data();
}

WorkspaceToolbar* WorkspaceWindow::header() const noexcept
{
    return m_header;
}

QMessageBox* WorkspaceWindow::question() const noexcept
{
    return m_question.data();
}

void WorkspaceWindow::closeWithoutAsking()
{
    m_closing = true;
    close();
}

void WorkspaceWindow::closeEvent(QCloseEvent* event)
{
    if (m_closing)
    {
        QWidget::closeEvent(event);
        return;
    }
    // The workspace goes back to the main window, or its images close: the person says which.
    event->ignore();
    ask();
}

void WorkspaceWindow::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event != nullptr && event->type() == QEvent::ActivationChange && isActiveWindow() && m_hooks.activated)
    {
        m_hooks.activated();
    }
}

void WorkspaceWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event != nullptr && event->mimeData() != nullptr && m_hooks.takesFiles &&
        m_hooks.takesFiles(*event->mimeData()))
    {
        event->acceptProposedAction();
    }
}

void WorkspaceWindow::dropEvent(QDropEvent* event)
{
    if (event != nullptr && event->mimeData() != nullptr && m_hooks.filesDropped &&
        m_hooks.filesDropped(*event->mimeData()))
    {
        event->acceptProposedAction();
    }
}

void WorkspaceWindow::ask()
{
    if (!m_question.isNull())
    {
        m_question->raise();
        return;
    }
    auto* question = new QMessageBox(QMessageBox::Question, windowTitle(),
                                     tr("Return this workspace to the main window, or close its images?"),
                                     QMessageBox::NoButton, this);
    question->setObjectName(QStringLiteral("workspaceWindowQuestion"));
    question->setAttribute(Qt::WA_DeleteOnClose);
    QPushButton* const returnButton = question->addButton(tr("Return"), QMessageBox::AcceptRole);
    returnButton->setObjectName(QStringLiteral("returnWorkspaceButton"));
    QPushButton* const closeButton = question->addButton(tr("Close Images"), QMessageBox::DestructiveRole);
    closeButton->setObjectName(QStringLiteral("closeWorkspaceImagesButton"));
    question->addButton(QMessageBox::Cancel);
    question->setDefaultButton(returnButton);
    m_question = question;
    connect(question, &QMessageBox::buttonClicked, this, [this, returnButton, closeButton](QAbstractButton* button) {
        if (!m_hooks.answered)
        {
            return;
        }
        if (button == returnButton)
        {
            m_hooks.answered(Answer::Return);
        }
        else if (button == closeButton)
        {
            m_hooks.answered(Answer::CloseImages);
        }
    });
    // Asked without waiting: the window is not closed while the question is open.
    question->open();
}

} // namespace an::ui::observatory
