// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The small dialog that renames a workspace.

#include "AfterNightShellObservatoryExport.h"

#include <QDialog>
#include <QString>

#include <functional>

class QDialogButtonBox;
class QLineEdit;

namespace an::ui::observatory
{

/// "Rename Workspace": the workspace's name in a field, all of it selected, and OK and Cancel; OK is offered only while
/// the name holds more than spaces. Opened window-modal; accepted, it hands the name to `renamed`.
class AFTERNIGHTSHELLOBSERVATORY_API RenameWorkspaceDialog final : public QDialog
{
public:
    RenameWorkspaceDialog(const QString& name, std::function<void(const QString& name)> renamed,
                          QWidget* parent = nullptr);

    [[nodiscard]] QLineEdit* nameField() const noexcept;
    [[nodiscard]] QDialogButtonBox* buttons() const noexcept;

private:
    QLineEdit* m_name = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
};

} // namespace an::ui::observatory
