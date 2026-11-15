// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "workspaces/RenameWorkspaceDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

namespace an::ui::observatory
{

RenameWorkspaceDialog::RenameWorkspaceDialog(const QString& name, std::function<void(const QString& name)> renamed,
                                             QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("renameWorkspaceDialog"));
    setWindowTitle(tr("Rename Workspace"));
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowModality(Qt::WindowModal);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    m_name = new QLineEdit(name, this);
    m_name->setObjectName(QStringLiteral("workspaceNameField"));
    m_name->selectAll();
    form->addRow(tr("Name"), m_name);
    layout->addLayout(form);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(m_buttons);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    // A name of spaces alone is no name.
    const auto offerOk = [this]() {
        m_buttons->button(QDialogButtonBox::Ok)->setEnabled(!m_name->text().trimmed().isEmpty());
    };
    connect(m_name, &QLineEdit::textChanged, this, offerOk);
    offerOk();
    connect(this, &QDialog::accepted, this, [this, renamed = std::move(renamed)]() {
        if (renamed)
        {
            renamed(m_name->text().trimmed());
        }
    });
}

QLineEdit* RenameWorkspaceDialog::nameField() const noexcept
{
    return m_name;
}

QDialogButtonBox* RenameWorkspaceDialog::buttons() const noexcept
{
    return m_buttons;
}

} // namespace an::ui::observatory
