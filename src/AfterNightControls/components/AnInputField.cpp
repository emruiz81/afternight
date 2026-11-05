// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "components/AnInputField.h"

#include "theme/AnDesignTokens.h"

#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QVBoxLayout>

namespace an::ui
{

AnInputField::AnInputField(const QString& labelText, const QString& placeholderText, QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("anInputField"));

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(4);

    m_label = new QLabel(this);
    m_label->setObjectName(QStringLiteral("anInputFieldLabel"));
    m_label->setWordWrap(false);
    rootLayout->addWidget(m_label);

    m_lineEdit = new QLineEdit(this);
    m_lineEdit->setObjectName(QStringLiteral("anInputFieldLineEdit"));
    rootLayout->addWidget(m_lineEdit);

    setLabelText(labelText);
    setPlaceholderText(placeholderText);
    refreshTheme();
}

void AnInputField::setLabelText(const QString& text)
{
    if (!m_label)
    {
        return;
    }

    m_label->setText(text.trimmed());
    updateLabelVisibility();
}

QString AnInputField::labelText() const
{
    return m_label ? m_label->text() : QString();
}

QLabel* AnInputField::label() const
{
    return m_label;
}

void AnInputField::setText(const QString& text)
{
    if (!m_lineEdit)
    {
        return;
    }

    m_lineEdit->setText(text);
}

QString AnInputField::text() const
{
    return m_lineEdit ? m_lineEdit->text() : QString();
}

void AnInputField::setPlaceholderText(const QString& text)
{
    if (!m_lineEdit)
    {
        return;
    }

    m_lineEdit->setPlaceholderText(text.trimmed());
}

QString AnInputField::placeholderText() const
{
    return m_lineEdit ? m_lineEdit->placeholderText() : QString();
}

QLineEdit* AnInputField::lineEdit() const
{
    return m_lineEdit;
}

void AnInputField::refreshTheme()
{
    if (!m_label || !m_lineEdit)
    {
        return;
    }

    refreshTokens();
    const auto& tokenSet = tokens();

    if (auto* rootLayout = qobject_cast<QVBoxLayout*>(layout()))
    {
        rootLayout->setSpacing(tokenSet.spacing.xs);
    }

    m_label->setFont(tokenSet.typography.caption);
    m_lineEdit->setFont(tokenSet.typography.body);

    // Label color via palette
    QPalette labelPal = m_label->palette();
    labelPal.setColor(QPalette::WindowText, tokenSet.colors.textSecondary);
    m_label->setPalette(labelPal);

    // Re-polish the line edit so the proxy style paints it correctly
    m_lineEdit->style()->unpolish(m_lineEdit);
    m_lineEdit->style()->polish(m_lineEdit);
    m_lineEdit->update();
}

void AnInputField::updateLabelVisibility()
{
    if (!m_label)
    {
        return;
    }

    m_label->setVisible(!m_label->text().isEmpty());
}

} // namespace an::ui
