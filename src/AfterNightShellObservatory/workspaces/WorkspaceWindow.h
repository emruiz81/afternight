// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A workspace moved to a window of its own.

#include "AfterNightShellObservatoryExport.h"

#include <QPointer>
#include <QString>
#include <QWidget>

#include <functional>

class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QEvent;
class QMessageBox;
class QMimeData;
class QVBoxLayout;

namespace an::ui::observatory
{

class WorkspacePage;
class WorkspaceToolbar;

/// A top-level window titled with its workspace's name: a header with the layout icons, then the workspace's page - its
/// dock area, its floating toolbar and its history strip - as the window's centre showed it. Activated, it makes its
/// workspace the current one. Closing it asks "Return this workspace to the main window, or close its images?" and
/// does what the answer says through `Hooks`; it stays while the question is open, and when it is cancelled.
class AFTERNIGHTSHELLOBSERVATORY_API WorkspaceWindow final : public QWidget
{
public:
    /// What the question's answer asks of the shell's window.
    enum class Answer
    {
        Return,
        CloseImages
    };

    struct Hooks
    {
        /// The window became the active one.
        std::function<void()> activated;
        /// The person answered the question its closing asked.
        std::function<void(Answer answer)> answered;
        /// Files were dropped on the window.
        std::function<bool(const QMimeData& files)> filesDropped;
        /// Whether a drag carries files the window opens.
        std::function<bool(const QMimeData& files)> takesFiles;
    };

    /// Shows `page`, which the window holds from here until `takePage`, under `header`, which it owns.
    WorkspaceWindow(const QString& title, WorkspaceToolbar& header, WorkspacePage& page, Hooks hooks);
    ~WorkspaceWindow() override;

    WorkspaceWindow(const WorkspaceWindow&) = delete;
    WorkspaceWindow& operator=(const WorkspaceWindow&) = delete;

    /// Gives the page back; the window shows none from then on.
    [[nodiscard]] WorkspacePage* takePage();
    [[nodiscard]] WorkspacePage* page() const noexcept;
    [[nodiscard]] WorkspaceToolbar* header() const noexcept;
    /// The question its closing asked, while it is open; null when none is.
    [[nodiscard]] QMessageBox* question() const noexcept;

    /// Closes the window without asking anything: what the shell does once the answer is carried out.
    void closeWithoutAsking();

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void ask();

    Hooks m_hooks;
    QVBoxLayout* m_layout = nullptr;
    WorkspaceToolbar* m_header = nullptr;
    QPointer<WorkspacePage> m_page;
    QPointer<QMessageBox> m_question;
    bool m_closing = false;
};

} // namespace an::ui::observatory
