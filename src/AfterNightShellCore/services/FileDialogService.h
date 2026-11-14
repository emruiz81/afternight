// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The file and folder dialogs the application opens, and the folders they remember.

#include "AfterNightShellCoreExport.h"

#include "controls/IAppContext.h"

#include <QFileDialog>
#include <QString>
#include <QStringList>

class QWidget;

namespace an::ui::shell
{

class ApplicationCore;

/// Opens the application's file dialogs, each modal over the window it belongs to (`dialogParentFor`): drawn by the
/// system or by Qt as the setting `interface/useSystemFileBrowser` says, opened in the folder its context remembers -
/// or, with no context or none remembered, the folder opened last - and remembering under its context the folder the
/// person chose from. Used on the GUI thread.
class AFTERNIGHTSHELLCORE_API FileDialogService final : public an::ui::IFileDialogService
{
public:
    explicit FileDialogService(ApplicationCore& core);

    // ---- IFileDialogService

    /// The image files the person chose to open; files of a kind the application cannot open are left out.
    [[nodiscard]] QStringList selectImageFiles(QWidget* parent, const QString& title, const QString& folderContext,
                                               bool supportMultipleFiles = true) override;

    /// The files the person chose, of the given name filters when any are given.
    [[nodiscard]] QStringList selectFiles(QWidget* parent, const QString& title, const QString& folderContext,
                                          const QStringList& fileExtensions) override;

    /// The file the person chose to save to. `defaultDir`, when it is a folder, is where the dialog opens; otherwise
    /// the folder its context remembers. The folder chosen is remembered under the context whenever one is given.
    [[nodiscard]] QStringList saveFiles(QWidget* parent, const QString& title, const QString& defaultDir,
                                        const QString& folderContext, const QStringList& fileExtensions) override;

    /// The folder the person chose, starting from `dir`; empty when the dialog was cancelled.
    [[nodiscard]] QString selectDirectory(QWidget* parent, const QString& title = QString(),
                                          const QString& dir = QString(),
                                          QFileDialog::Options options = QFileDialog::ShowDirsOnly);

private:
    ApplicationCore& m_core;
};

} // namespace an::ui::shell
