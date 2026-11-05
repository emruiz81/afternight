// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "components/AnSectionHeader.h"

#include "theme/AnDesignTokens.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace an::ui
{

AnSectionHeader::AnSectionHeader(const QString& title, const QString& description, const QString& actionText,
                                 QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("anSectionHeader"));

    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);

    auto* textContainer = new QWidget(this);
    textContainer->setObjectName(QStringLiteral("anSectionHeaderTextContainer"));
    auto* textLayout = new QVBoxLayout(textContainer);
    textLayout->setContentsMargins(0, 0, 0, 0);

    m_titleLabel = new QLabel(textContainer);
    m_titleLabel->setObjectName(QStringLiteral("anSectionHeaderTitle"));
    m_titleLabel->setWordWrap(true);
    textLayout->addWidget(m_titleLabel);

    m_descriptionLabel = new QLabel(textContainer);
    m_descriptionLabel->setObjectName(QStringLiteral("anSectionHeaderDescription"));
    m_descriptionLabel->setWordWrap(true);
    textLayout->addWidget(m_descriptionLabel);

    m_actionButton = new QPushButton(this);
    m_actionButton->setObjectName(QStringLiteral("anSectionHeaderActionButton"));
    m_actionButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);

    rootLayout->addWidget(textContainer, 1);
    rootLayout->addWidget(m_actionButton, 0, Qt::AlignRight | Qt::AlignTop);

    setTitle(title);
    setDescription(description);
    setActionText(actionText);
    refreshTheme();
}

void AnSectionHeader::setTitle(const QString& title)
{
    if (!m_titleLabel)
    {
        return;
    }

    m_titleLabel->setText(title.trimmed());
}

QString AnSectionHeader::title() const
{
    return m_titleLabel ? m_titleLabel->text() : QString();
}

QLabel* AnSectionHeader::titleLabel() const
{
    return m_titleLabel;
}

void AnSectionHeader::setDescription(const QString& description)
{
    if (!m_descriptionLabel)
    {
        return;
    }

    m_descriptionLabel->setText(description.trimmed());
    updateDescriptionVisibility();
}

QString AnSectionHeader::description() const
{
    return m_descriptionLabel ? m_descriptionLabel->text() : QString();
}

QLabel* AnSectionHeader::descriptionLabel() const
{
    return m_descriptionLabel;
}

void AnSectionHeader::setActionText(const QString& actionText)
{
    if (!m_actionButton)
    {
        return;
    }

    m_actionButton->setText(actionText.trimmed());
    updateActionVisibility();
}

QString AnSectionHeader::actionText() const
{
    return m_actionButton ? m_actionButton->text() : QString();
}

QPushButton* AnSectionHeader::actionButton() const
{
    return m_actionButton;
}

void AnSectionHeader::refreshTheme()
{
    if (!m_titleLabel || !m_descriptionLabel || !m_actionButton)
    {
        return;
    }

    refreshTokens();
    const auto& tokenSet = tokens();

    if (auto* rootLayout = qobject_cast<QHBoxLayout*>(layout()))
    {
        rootLayout->setSpacing(tokenSet.spacing.sm);
    }

    if (auto* textContainer = findChild<QWidget*>(QStringLiteral("anSectionHeaderTextContainer")))
    {
        if (auto* textLayout = qobject_cast<QVBoxLayout*>(textContainer->layout()))
        {
            textLayout->setSpacing(tokenSet.spacing.xs);
        }
    }

    m_titleLabel->setFont(tokenSet.typography.heading2);
    m_descriptionLabel->setFont(tokenSet.typography.body);
    m_actionButton->setFont(tokenSet.typography.caption);

    // Label colors via palette
    QPalette titlePal = m_titleLabel->palette();
    titlePal.setColor(QPalette::WindowText, tokenSet.colors.textPrimary);
    m_titleLabel->setPalette(titlePal);

    QPalette descPal = m_descriptionLabel->palette();
    descPal.setColor(QPalette::WindowText, tokenSet.colors.textSecondary);
    m_descriptionLabel->setPalette(descPal);

    // Re-polish the action button so the proxy style paints it correctly
    m_actionButton->style()->unpolish(m_actionButton);
    m_actionButton->style()->polish(m_actionButton);
    m_actionButton->update();
}

void AnSectionHeader::updateDescriptionVisibility()
{
    if (!m_descriptionLabel)
    {
        return;
    }

    m_descriptionLabel->setVisible(!m_descriptionLabel->text().isEmpty());
}

void AnSectionHeader::updateActionVisibility()
{
    if (!m_actionButton)
    {
        return;
    }

    m_actionButton->setVisible(!m_actionButton->text().isEmpty());
}

} // namespace an::ui
